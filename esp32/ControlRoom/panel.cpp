#include "panel.h"
#include "site_profile.h"
#include <ESP32epdx.h>
#include "EPD.h"

// The staged vendor example uses this full-frame buffer as its partial baseline.
unsigned char *PartImage;
static unsigned char *blackPlane;
static constexpr int rowBytes = EPD_WIDTH / 8;
static constexpr size_t planeBytes = EPD_HEIGHT * rowBytes;
static_assert(EPD_WIDTH == 800 && EPD_HEIGHT == 480, "v6 map requires the 800x480 panel");
// Approved v6 base: 550 x 400 mm, +x right, +y down. Uniform 0.9 pixel/mm.
static constexpr int mapLeft = 152, mapTop = 56;
static constexpr int mapX(int mm) { return mapLeft + (mm * 9 + 5) / 10; }
static constexpr int mapY(int mm) { return mapTop + (mm * 9 + 5) / 10; }
static constexpr int startX = mapX(280), startY = mapY(90);
static constexpr uint32_t busyLimitMs = 30000;
static bool timedOut, needsFull = true;
static int partialCount;
static uint32_t busyLowMs;
static uint32_t completedRefreshes;

uint32_t panelRefreshCount() {
    return completedRefreshes;
}

// Called by the staged monochrome GDEY075T7 driver at every manufacturer BUSY wait.
bool panelReady() {
    const uint32_t start = millis();
    busyLowMs = 0;
    while (!isEPD_W21_BUSY) {
        if (millis() - start >= busyLimitMs) {
            // Sample the input with a weak pull-up only after the vendor wait failed.
            pinMode(A14, INPUT_PULLUP);
            delay(1);
            const bool rises = isEPD_W21_BUSY;
            pinMode(A14, INPUT);
            Serial.println(rises
                ? "EPD: BUSY LOW for 30 s, rose with weak pull-up. Check P5 pin 8 / panel power."
                : "EPD: BUSY LOW for 30 s, still LOW with weak pull-up. Check adapter, ribbon and panel model.");
            timedOut = true;
            return false;
        }
        delay(1);
        busyLowMs = millis() - start;
    }
    return true;
}

bool startPanel() {
    blackPlane = static_cast<unsigned char *>(malloc(planeBytes));

    PartImage = static_cast<unsigned char *>(malloc(planeBytes));
    if (!blackPlane || !PartImage) return false;
    // ESP32-L P5 and the two other SPI clients (W25Q16, GT30L32S4W).
    for (int pin : {2, 15, 27}) {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    static_assert(A14 == 13 && A15 == 12 && A16 == 14 && A17 == 27, "Select the classic ESP32 Dev Module");
    pinMode(A14, INPUT);
    pinMode(A15, OUTPUT);
    pinMode(A16, OUTPUT);
    SPI.begin(18, 19, 23, -1);
    Paint_NewImage(blackPlane, EPD_WIDTH, EPD_HEIGHT, ROTATE_0, WHITE);
    return true;
}

// Physical coordinates are the calibrated STM32 millimetres, not a schematic stretch.
// Bounds are the four nominal v6 corridor interiors (corners intentionally overlap).
static constexpr int corridorBounds[4][4] = {
    {210, 20, 530, 160},
    {390, 20, 530, 380},
    {20, 240, 530, 380},
    {20, 160, 160, 380}
};

bool locate(const Position &p, int &x, int &y) {
    if (!p.valid || p.corridor < 1 || p.corridor > 4) return false;
    const SiteCorridor &c = site_corridors[p.corridor - 1];
    const bool horizontal = p.corridor == 1 || p.corridor == 3;
    const int direction = horizontal ? c.dx : c.dy;
    if (c.length_mm <= 0 || c.length_mm > 32500 || (direction != 1 && direction != -1) ||
        (horizontal ? c.dy : c.dx) != 0) return false;
    if ((horizontal && p.y != c.y_mm) || (!horizontal && p.x != c.x_mm)) return false;
    int64_t distance = horizontal ? (int64_t)p.x - c.x_mm : (int64_t)p.y - c.y_mm;
    distance *= direction;
    if (distance < 0 || distance > c.length_mm) return false;
    const int *b = corridorBounds[p.corridor - 1];
    if (p.x <= b[0] || p.x >= b[2] || p.y <= b[1] || p.y >= b[3]) return false;
    x = mapX(p.x);
    y = mapY(p.y);
    return true;
}

static void drawBoldText(int x, int y, const char *text, sFONT *font, uint16_t ink) {
    // The vendor painter treats WHITE as a transparent text background.
    Paint_DrawString_EN(x, y, text, font, ink, WHITE);
    Paint_DrawString_EN(x + 1, y, text, font, ink, WHITE);
}

// All rectangles are derived from docs/foam-v6-geometry.json, converted to mm.
// Order: x_min, y_min, x_max, y_max (top-left origin).
static constexpr int v6Walls[14][4] = {
    {350, 0, 550, 20}, // W01
    {180, 160, 390, 180}, // W02
    {530, 20, 550, 400}, // W03
    {0, 380, 265, 400}, // W04A
    {265, 380, 530, 400}, // W04B
    {0, 160, 20, 380}, // W05
    {160, 220, 390, 240}, // W06
    {370, 180, 390, 220}, // W07
    {160, 160, 180, 220}, // W08
    {0, 0, 210, 20}, // W09
    {0, 20, 20, 160}, // W10
    {190, 20, 210, 160}, // W11
    {20, 140, 40, 160}, // W12
    {140, 140, 190, 160}, // W13
};

static void drawMap(const Screen &s) {
    Paint_SelectImage(blackPlane);
    Paint_Clear(WHITE);
    for (const auto &w : v6Walls) {
        Paint_DrawRectangle(mapX(w[0]), mapY(w[1]), mapX(w[2])-1, mapY(w[3])-1,
                            BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    // Closed destination door from the v6 plan. Static map, not live door position.
    Paint_DrawRectangle(mapX(43), mapY(118), mapX(137)-1, mapY(138)-1,
                        BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_2X2);
    // Hazard: y=160..230 mm, critical band y=185..215 mm in physical coordinates.
    // In the top-left coordinate convention the critical band is y=185..215.
    // Keep the interior white for the tracked position; dashed outer and dotted inner lines.
    for (int x = mapX(390)+3; x+6 < mapX(530)-2; x += 12) {
        Paint_DrawRectangle(x, mapY(160), x+6, mapY(160)+1, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawRectangle(x, mapY(230)-1, x+6, mapY(230), BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    for (int x = mapX(390)+3; x+2 < mapX(530)-2; x += 8) {
        Paint_DrawRectangle(x, mapY(185), x+2, mapY(185)+1, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawRectangle(x, mapY(215)-1, x+2, mapY(215), BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    drawBoldText(362, 59, "ENTRY", &Font16, BLACK);
    drawBoldText(366, 104, "START", &Font16, BLACK);
    drawBoldText(193, 106, "ROOM", &Font20, BLACK);
    drawBoldText(193, 185, "DOOR", &Font16, BLACK);
    drawBoldText(356, 225, "CLOSED", &Font16, BLACK);
    drawBoldText(657, 127, "S1", &Font20, BLACK);
    drawBoldText(657, 216, "HAZARD", &Font16, BLACK);
    drawBoldText(657, 238, "S5", &Font20, BLACK);
    drawBoldText(95, 325, "S3", &Font20, BLACK);
    drawBoldText(216, 418, "S4", &Font16, BLACK);
    drawBoldText(549, 418, "S2", &Font16, BLACK);
    Paint_DrawCircle(startX, startY, 14, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawCircle(startX, startY, 10, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawCircle(startX, startY, 6, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    const int textWidth = EPD_WIDTH - 65;
    drawBoldText(32, 11, s.location, strlen(s.location) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK);
    drawBoldText(32, 445, s.state, strlen(s.state) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK);
    if (s.x >= 0) Paint_DrawCircle(s.x, s.y, 16, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_2X2);
}

struct Window {
    int firstByte = rowBytes, lastByte = -1, top = EPD_HEIGHT, bottom = -1;
};

static void restorePreviousFrame() {
    // Deep sleep loses the controller RAM. Seed both planes after waking.
    for (const uint8_t command : {0x10, 0x13}) {
        EPD_W21_WriteCMD(command);
        for (size_t i = 0; i < planeBytes; ++i) EPD_W21_WriteDATA(PartImage[i]);
    }
}

// EPD_Update waits for BUSY after command 0x12. Check its result before
// deep sleep or the next window performs another BUSY wait.
static bool refreshCompleted(const char *operation) {
    if (timedOut) return false;
    if (busyLowMs > 0) {
        ++completedRefreshes;
        return true;
    }
    Serial.printf("EPD: no BUSY pulse on %s refresh; check P5 BUSY and panel model.\n", operation);
    return false;
}

static bool drawPartial(const Window &w) {
    const int width = w.lastByte - w.firstByte + 1, height = w.bottom - w.top + 1;
    auto *pixels = static_cast<unsigned char *>(malloc(width * height));
    if (!pixels) return false;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y * width + x] = blackPlane[(w.top + y) * rowBytes + w.firstByte + x];
    EPD_Init_Part();
    if (!timedOut) {
        restorePreviousFrame();
        EPD_Dis_Part(w.firstByte * 8, w.top, pixels, height, width * 8);
    }
    const bool completed = refreshCompleted("partial");
    if (completed) {
        EPD_W21_WriteCMD(0x92); // partial-out, as in the vendor's clock example
        for (int y = 0; y < height; ++y)
            memcpy(PartImage + (w.top + y) * rowBytes + w.firstByte, pixels + y * width, width);
        ++partialCount;
    }
    free(pixels);
    return completed;
}

PanelUpdate drawScreen(const Screen &s, bool forceFull, bool fullAllowed) {
    timedOut = false;
    if (forceFull) needsFull = true;
    drawMap(s);
    Window windows[3];
    int changed = 0;
    if (!needsFull) {
        for (int y = 0; y < EPD_HEIGHT; ++y) {
            Window &w = windows[y < 45 ? 0 : (y < 434 ? 1 : 2)];
            for (int x = 0; x < rowBytes; ++x) {
                const int i = y * rowBytes + x;
                if (PartImage[i] == blackPlane[i]) continue;
                w.firstByte = min(w.firstByte, x); w.lastByte = max(w.lastByte, x);
                w.top = min(w.top, y); w.bottom = y;
            }
        }
        for (const Window &w : windows) {
            if (w.bottom < 0) continue;
            ++changed;
        }
        if (!changed) return PanelUpdate::Done;
        if (partialCount + changed > 5) needsFull = true;
    }
    if (needsFull && !fullAllowed) return PanelUpdate::NeedsFull;
    const bool full = needsFull;
    // Leave signal timing margin across the adapter and panel ribbon.
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    bool completed = true;
    if (full) {
        Serial.println("EPD: starting full refresh (T075A04 mono, SPI 4 MHz).");
        EPD_Init();
        if (!timedOut) {
            Serial.println("EPD: panel initialized; sending map.");
            EPD_WhiteScreen_ALL(blackPlane);
            completed = refreshCompleted("map");
            if (completed) {
                memcpy(PartImage, blackPlane, planeBytes);
                Serial.printf("EPD: refresh command sent; BUSY wait %lu ms.\n", (unsigned long)busyLowMs);
            }
        }
        partialCount = 0;
    } else {
        for (const Window &w : windows) {
            if (w.bottom >= 0 && !drawPartial(w)) {
                completed = false;
                break;
            }
        }
    }
    if (!timedOut) {
        EPD_DeepSleep();
        if (full && !timedOut) Serial.println("EPD: panel power off; deep sleep command sent.");
    }
    SPI.endTransaction();
    needsFull = timedOut || !completed;
    if (needsFull) return PanelUpdate::Failed;
    return full ? PanelUpdate::Full : PanelUpdate::Done;
}
