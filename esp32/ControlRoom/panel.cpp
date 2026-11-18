#include "panel.h"
#include "site_profile.h"
#include <ESP32epdx.h>
#include "EPD.h"

// The staged vendor example uses this full-frame buffer as its partial baseline.
unsigned char *PartImage;
static unsigned char *blackPlane, *redPlane;
static constexpr int rowBytes = EPD_WIDTH / 8;
static constexpr size_t planeBytes = EPD_HEIGHT * rowBytes;
static constexpr int startX = 100, startY = 110;
static constexpr uint32_t busyLimitMs = 30000;
static bool timedOut, needsFull = true;
static bool clearedAtStartup;
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
    redPlane = static_cast<unsigned char *>(malloc(planeBytes));
    PartImage = static_cast<unsigned char *>(malloc(planeBytes));
    if (!blackPlane || !redPlane || !PartImage) return false;
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

static void makePlanes(const Screen &s) {
    Paint_SelectImage(blackPlane);
    Paint_Clear(WHITE);
    Paint_DrawRectangle(32, 45, 768, 432, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_2X2);
    // Join the four corridors before filling their interior so corners stay open.
    Paint_DrawRectangle(80, 90, 670, 130, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(630, 90, 670, 270, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(250, 230, 670, 270, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(250, 230, 290, 405, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(84, 94, 666, 126, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(634, 94, 666, 266, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(254, 234, 666, 266, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(254, 234, 286, 401, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawString_EN(688, 146, "S2", &Font24, BLACK, WHITE);
    Paint_DrawString_EN(688, 235, "S3", &Font24, BLACK, WHITE);
    Paint_DrawString_EN(312, 374, "S4 / DOOR", &Font24, BLACK, WHITE);
    // Keep even the longest age or status on one native-pixel line.
    const int textWidth = EPD_WIDTH - 64;
    Paint_DrawString_EN(32, 11, s.location, strlen(s.location) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK, WHITE);
    Paint_DrawString_EN(32, 445, s.state, strlen(s.state) * Font24.Width <= textWidth ? &Font24 : &Font20, BLACK, WHITE);
    if (s.x >= 0) {
        Paint_DrawCircle(s.x, s.y, 18, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawCircle(s.x, s.y, 13, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    Paint_SelectImage(redPlane);
    Paint_Clear(WHITE);
    Paint_DrawString_EN(80, 56, "START / S1", &Font24, RED, WHITE);
    Paint_DrawCircle(startX, startY, 15, RED, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    for (int x = 395; x < 570; x += 22) {
        Paint_DrawRectangle(x, 206, x + 14, 210, RED, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawRectangle(x, 290, x + 14, 294, RED, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    Paint_DrawString_EN(395, 172, "HAZARD ZONE", &Font24, RED, WHITE);
    Paint_DrawString_EN(460, 309, "S5", &Font24, RED, WHITE);
    // EPD_SetRAMValue_BaseMap accepts ink=1; Good Display GUI emits ink=0.
    for (size_t i = 0; i < planeBytes; ++i) {
        redPlane[i] = ~redPlane[i];
        blackPlane[i] = static_cast<uint8_t>(~blackPlane[i] & ~redPlane[i]);
    }
}

struct Window {
    int firstByte = rowBytes, lastByte = -1, top = EPD_HEIGHT, bottom = -1;
};

static uint8_t composite(size_t i) {
    return static_cast<uint8_t>(~(blackPlane[i] | redPlane[i]));
}

static bool hasRed(const Window &w) {
    for (int y = w.top; y <= w.bottom; ++y)
        for (int x = w.firstByte; x <= w.lastByte; ++x)
            if (redPlane[y * rowBytes + x]) return true;
    return false;
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
            pixels[y * width + x] = composite((w.top + y) * rowBytes + w.firstByte + x);
    // Init restores RAM from the last physically completed update before each window.
    EPD_Init_Part();
    if (!timedOut) EPD_Dis_Part(w.firstByte * 8, w.top, pixels, height, width * 8);
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

static bool clearAtStartup() {
    // Exercise both ink states across the old image with the GDEY075Z08
    // driver's full black and white frames. Resume at a failed frame on retry.
    static unsigned int finishedFrames;
    while (finishedFrames < 2) {
        const bool black = finishedFrames == 0;
        const char *color = black ? "black" : "white";
        Serial.printf("EPD: initializing for %s clear.\n", color);
        SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
        EPD_Init();
        bool refreshObserved = false;
        if (!timedOut) {
            Serial.printf("EPD: sending %s frame.\n", color);
            if (black) EPD_WhiteScreen_Black();
            else EPD_WhiteScreen_White();
            refreshObserved = refreshCompleted(color);
            if (refreshObserved)
                Serial.printf("EPD: %s refresh BUSY wait %lu ms.\n", color, (unsigned long)busyLowMs);
            if (!timedOut) EPD_DeepSleep();
        }
        SPI.endTransaction();
        if (timedOut || !refreshObserved) return false;
        Serial.printf("EPD: %s refresh complete.\n", color);
        ++finishedFrames;
        delay(3000);  // Sleep interval between full frames in the manufacturer's example.
    }
    return true;
}

bool drawScreen(const Screen &s, bool forceFull) {
    timedOut = false;
    if (forceFull) needsFull = true;
    if (!clearedAtStartup) {
        clearedAtStartup = clearAtStartup();
        if (!clearedAtStartup) return false;
    }
    makePlanes(s);
    Window windows[3];
    int changed = 0;
    if (!needsFull) {
        for (int y = 0; y < EPD_HEIGHT; ++y) {
            Window &w = windows[y < 45 ? 0 : (y < 434 ? 1 : 2)];
            for (int x = 0; x < rowBytes; ++x) {
                const int i = y * rowBytes + x;
                if (PartImage[i] == composite(i)) continue;
                w.firstByte = min(w.firstByte, x); w.lastByte = max(w.lastByte, x);
                w.top = min(w.top, y); w.bottom = y;
            }
        }
        for (const Window &w : windows) {
            if (w.bottom < 0) continue;
            ++changed;
            if (hasRed(w)) needsFull = true;
        }
        if (!changed) return true;
        if (partialCount + changed > 5) needsFull = true;
    }
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    bool completed = true;
    if (needsFull) {
        Serial.println("EPD: starting full refresh (GDEY075Z08).");
        EPD_Init();
        if (!timedOut) {
            Serial.println("EPD: panel initialized; sending map.");
            EPD_SetRAMValue_BaseMap(blackPlane, redPlane);
            completed = refreshCompleted("map");
            if (completed)
                Serial.printf("EPD: refresh command sent; BUSY wait %lu ms.\n", (unsigned long)busyLowMs);
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
    if (!timedOut) EPD_DeepSleep();
    SPI.endTransaction();
    needsFull = timedOut || !completed;
    return !needsFull;
}
