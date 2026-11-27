#include "panel.h"
#include <math.h>
#include <ESP32epdx.h>
#include "EPD.h"

// The staged vendor example uses this full-frame buffer as its partial baseline.
unsigned char *PartImage;
static unsigned char *blackPlane;
static constexpr int rowBytes = EPD_WIDTH / 8;
static constexpr size_t planeBytes = EPD_HEIGHT * rowBytes;
static_assert(EPD_WIDTH == 800 && EPD_HEIGHT == 480, "ScanDisplay requires the 800x480 panel");
static constexpr uint32_t busyLimitMs = 30000;
static bool timedOut, needsFull = true;
static int partialCount;
static uint32_t busyLowMs;

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
    if (!blackPlane || !PartImage) {
        free(blackPlane);
        free(PartImage);
        blackPlane = PartImage = nullptr;
        return false;
    }
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


// The dial represents the STM32 servo command. A small ultrasonic sensor does not
// supply an obstacle's exact bearing. The distance is shown numerically.
static void drawFrame(const Screen &s) {
    Paint_SelectImage(blackPlane);
    Paint_Clear(WHITE);
    Paint_DrawString_EN(28, 22, "ULTRASONIC SCANNER", &Font24, BLACK, WHITE);
    Paint_DrawLine(28, 61, 770, 61, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    const char *link = s.link == LinkState::Online ? "RECEIVING STM32" :
        (s.link == LinkState::Offline ? "STM32 LINK LOST" : "WAITING FOR STM32");
    Paint_DrawString_EN(28, 74, link, &Font20, BLACK, WHITE);
    const int cx = 202, cy = 320, radius = 148;
    int prevX = cx - radius, prevY = cy;
    for (int a = 5; a <= 180; a += 5) {
        const float rad = a * (PI / 180.0f);
        const int x = cx - lroundf(radius * cosf(rad));
        const int y = cy - lroundf(radius * sinf(rad));
        Paint_DrawLine(prevX, prevY, x, y, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_1X1);
        prevX = x; prevY = y;
    }
    Paint_DrawLine(cx - radius, cy, cx + radius, cy, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_1X1);
    Paint_DrawString_EN(47, 335, "0", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(187, 147, "90", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(319, 335, "180", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(88, 375, "SERVO COMMAND (DEG)", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(400, 149, "LAST DETECTION", &Font24, BLACK, WHITE);
    if (!s.haveHit) {
        Paint_DrawString_EN(400, 220, "No hit received", &Font20, BLACK, WHITE);
        return;
    }
    const float theta = s.angle * (PI / 180.0f);
    const int x = cx - lroundf((radius - 8) * cosf(theta));
    const int y = cy - lroundf((radius - 8) * sinf(theta));
    Paint_DrawLine(cx, cy, x, y, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
    Paint_DrawCircle(x, y, 7, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    char line[48];
    snprintf(line, sizeof(line), "Distance: %d mm", s.distanceMm);
    Paint_DrawString_EN(400, 222, line, &Font24, BLACK, WHITE);
    snprintf(line, sizeof(line), "Angle: %d deg", s.angle);
    Paint_DrawString_EN(400, 269, line, &Font24, BLACK, WHITE);
    snprintf(line, sizeof(line), "At ESP uptime: %llu s", (unsigned long long)s.hitUptimeS);
    Paint_DrawString_EN(400, 345, line, &Font16, BLACK, WHITE);
    Paint_DrawString_EN(28, 432, "Latest recorded hit; the target may have moved.", &Font16, BLACK, WHITE);
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


PanelUpdate drawScreen(const Screen &screen, bool fullAllowed) {
    timedOut = false;
    drawFrame(screen);
    Window w;
    if (!needsFull) {
        for (int y = 0; y < EPD_HEIGHT; ++y) {
            for (int byte = 0; byte < rowBytes; ++byte) {
                const int offset = y * rowBytes + byte;
                if (PartImage[offset] == blackPlane[offset]) continue;
                w.firstByte = min(w.firstByte, byte);
                w.lastByte = max(w.lastByte, byte);
                w.top = min(w.top, y);
                w.bottom = y;
            }
        }
        if (w.bottom < 0) return PanelUpdate::Done;
        if (partialCount >= 5) needsFull = true;
    }
    if (needsFull && !fullAllowed) return PanelUpdate::WaitForFull;
    const bool full = needsFull;
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    bool ok = false;
    if (full) {
        EPD_Init();
        if (!timedOut) {
            EPD_WhiteScreen_ALL(blackPlane);
            ok = refreshCompleted("full");
        }
        if (ok) {
            memcpy(PartImage, blackPlane, planeBytes);
            partialCount = 0;
        }
    } else {
        ok = drawPartial(w);
    }
    if (!timedOut) EPD_DeepSleep();
    SPI.endTransaction();
    needsFull = timedOut || !ok;
    if (needsFull) return PanelUpdate::Failed;
    return full ? PanelUpdate::Full : PanelUpdate::Done;
}

