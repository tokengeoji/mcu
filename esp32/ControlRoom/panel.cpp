#include "panel.h"
#include "site_profile.h"
#include <ESP32epdx.h>
#include "EPD.h"

// The staged vendor example uses this full-frame buffer as its partial baseline.
unsigned char *PartImage;
static unsigned char *blackPlane, *redPlane;
static constexpr int rowBytes = EPD_WIDTH / 8;
static constexpr size_t planeBytes = EPD_HEIGHT * rowBytes;
static constexpr uint32_t busyLimitMs = 30000;
static bool timedOut, needsFull = true;
static bool clearedAtStartup;
static int partialCount;
static uint32_t busyLowMs;

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
    const int left[] = {100, 650, 270, 270}, top[] = {110, 110, 250, 250};
    const int span[] = {550, 140, 380, 135};
    const int i = p.corridor - 1, delta = fraction * span[i] / c.length_mm;
    x = left[i] + (horizontal ? delta : 0);
    y = top[i] + (horizontal ? 0 : delta);
    return true;
}

static void makePlanes(const Screen &s) {
    Paint_SelectImage(blackPlane);
    Paint_Clear(WHITE);
    Paint_DrawRectangle(42, 45, 758, 432, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_2X2);
    // The four centre lines and labels follow the 800 x 480 map in the supplied HTML.
    Paint_DrawRectangle(85, 89, 671, 131, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(629, 110, 671, 271, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(249, 229, 650, 271, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawRectangle(249, 250, 291, 401, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawString_EN(80, 58, "START / S1", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(688, 150, "S2", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(692, 236, "S3", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(310, 380, "S4 / DOOR", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(44, 13, s.location, &Font16, BLACK, WHITE);
    Paint_DrawString_EN(44, 448, s.state, &Font16, BLACK, WHITE);
    if (s.x >= 0) {
        Paint_DrawCircle(s.x, s.y, 12, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
        Paint_DrawCircle(s.x, s.y, 9, WHITE, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    }
    Paint_SelectImage(redPlane);
    Paint_Clear(WHITE);
    Paint_DrawLine(395, 209, 570, 209, RED, LINE_STYLE_DOTTED, DOT_PIXEL_3X3);
    Paint_DrawLine(395, 291, 570, 291, RED, LINE_STYLE_DOTTED, DOT_PIXEL_3X3);
    Paint_DrawString_EN(407, 180, "HAZARD ZONE", &Font16, RED, WHITE);
    Paint_DrawString_EN(436, 313, "S5", &Font16, RED, WHITE);
    // EPD_SetRAMValue_BaseMap accepts ink=1; Good Display GUI emits ink=0.
    for (size_t i = 0; i < planeBytes; ++i) {
        blackPlane[i] = ~blackPlane[i];
        redPlane[i] = ~redPlane[i];
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
    if (!timedOut) {
        EPD_W21_WriteCMD(0x92); // partial-out, as in the vendor's clock example
        for (int y = 0; y < height; ++y)
            memcpy(PartImage + (w.top + y) * rowBytes + w.firstByte, pixels + y * width, width);
        ++partialCount;
    }
    free(pixels);
    return !timedOut;
}

static bool clearAtStartup() {
    // GDEY075Z08 example: initialize, clear both color planes, sleep, wait 3 s.
    Serial.println("EPD: initializing for white clear.");
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    EPD_Init();
    bool refreshObserved = false;
    if (!timedOut) {
        Serial.println("EPD: sending white frame.");
        EPD_WhiteScreen_White();
        refreshObserved = !timedOut && busyLowMs > 0;
        if (!timedOut) EPD_DeepSleep();
    }
    SPI.endTransaction();
    if (timedOut) return false;
    if (!refreshObserved) {
        Serial.println("EPD: no BUSY pulse on white refresh; check P5 BUSY and panel model.");
        return false;
    }
    Serial.println("EPD: white refresh complete.");
    delay(3000);
    return true;
}

bool drawScreen(const Screen &s) {
    timedOut = false;
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
    if (needsFull) {
        Serial.println("EPD: starting full refresh (GDEY075Z08).");
        EPD_Init();
        if (!timedOut) {
            Serial.println("EPD: panel initialized; sending map.");
            EPD_SetRAMValue_BaseMap(blackPlane, redPlane);
            if (!timedOut)
                Serial.printf("EPD: refresh command sent; BUSY wait %lu ms.\n", (unsigned long)busyLowMs);
        }
        partialCount = 0;
    } else {
        for (const Window &w : windows) {
            if (w.bottom >= 0 && !drawPartial(w)) {
                needsFull = true;
                if (!timedOut) EPD_DeepSleep();
                SPI.endTransaction();
                return false;
            }
        }
    }
    if (!timedOut) EPD_DeepSleep();
    SPI.endTransaction();
    needsFull = timedOut;
    return !timedOut;
}
