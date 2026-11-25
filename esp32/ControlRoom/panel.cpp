#include "panel.h"
#include "site_profile.h"
#include <ESP32epdx.h>
#include "EPD.h"

// The staged vendor example uses this full-frame buffer as its partial baseline.
unsigned char *PartImage;
static unsigned char *blackPlane;
static constexpr int rowBytes = EPD_WIDTH / 8;
static constexpr size_t planeBytes = EPD_HEIGHT * rowBytes;
static constexpr int startX = 100, startY = 110;
static constexpr uint32_t busyLimitMs = 30000;
static bool timedOut, needsFull = true;
static int partialCount;
static uint32_t busyLowMs;
static uint32_t completedRefreshes;

uint32_t panelRefreshCount() {
    return completedRefreshes;
}

// Called by the staged GDEY075Z08 driver at every manufacturer BUSY wait.
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

// Each measured axis maps onto the corresponding corridor in the design sketch.
// Rendering the corridor schematically keeps S5 and the location marker apart.
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
    const int fraction = direction > 0 ? (int)distance : c.length_mm - (int)distance;
    const int left[] = {startX, 650, 270, 270}, top[] = {startY, startY, 250, 250};
    const int span[] = {550, 140, 380, 135};
    const int i = p.corridor - 1, delta = fraction * span[i] / c.length_mm;
    x = left[i] + (horizontal ? delta : 0);
    y = top[i] + (horizontal ? 0 : delta);
    return true;
}

static void drawBoldText(int x, int y, const char *text, sFONT *font, uint16_t ink) {
    // The vendor painter treats WHITE as a transparent text background.
    Paint_DrawString_EN(x, y, text, font, ink, WHITE);
    Paint_DrawString_EN(x + 1, y, text, font, ink, WHITE);
}

static void drawMap(const Screen &s) {
    Paint_SelectImage(blackPlane);
    Paint_Clear(WHITE);
    Paint_DrawRectangle(32, 45, 768, 432, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_4X4);
    // Join the four corridors before filling their interior so corners stay open.
    Paint_DrawRectangle(80, 90, 670, 130, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(630, 90, 670, 270, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(250, 230, 670, 270, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(250, 230, 290, 405, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(88, 98, 662, 122, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(638, 98, 662, 262, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(258, 238, 662, 262, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(258, 238, 282, 397, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    drawBoldText(80, 56, "START / S1", &Font24, BLACK);
    Paint_DrawCircle(startX, startY, 21, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawCircle(startX, startY, 16, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    drawBoldText(688, 146, "S2", &Font24, BLACK);
    drawBoldText(688, 235, "S3", &Font24, BLACK);
    drawBoldText(312, 374, "S4 / DOOR", &Font24, BLACK);
    drawBoldText(460, 309, "S5", &Font24, BLACK);
    // Keep even the longest age or status on one native-pixel line.
    const int textWidth = EPD_WIDTH - 65;
    drawBoldText(32, 11, s.location, strlen(s.location) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK);
    drawBoldText(32, 445, s.state, strlen(s.state) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK);
    if (s.x >= 0) {
        Paint_DrawCircle(s.x, s.y, 23, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_3X3);
    }
    Paint_DrawCircle(startX, startY, 11, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    for (int x = 395; x < 570; x += 22) {
        Paint_DrawRectangle(x, 205, x + 16, 212, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawRectangle(x, 289, x + 16, 296, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    drawBoldText(395, 172, "HAZARD ZONE", &Font24, BLACK);
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
