#include "display_app.h"
#include "panel.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>

namespace {
constexpr int64_t usPerSecond = 1000000;
constexpr int64_t linkTimeout = 3 * usPerSecond;
constexpr int64_t shortGap = 2 * usPerSecond;
constexpr int64_t repeatGap = 36 * usPerSecond;
constexpr int64_t fullGap = 180 * usPerSecond;

struct Feed {
    bool received;
    bool haveHit;
    int hitAngle;
    int hitMm;
    int64_t lastFrameUs;
    int64_t lastHitUs;
    uint32_t episode;
    uint64_t hitUptimeS;
};
QueueHandle_t newestFeed;
bool panelStarted;
bool haveDisplayed;
uint32_t displayedEpisode;
Screen displayed{};
int64_t lastDisplayUs;
int64_t nextDrawUs;
int64_t nextFullUs;

// Parse unsigned ASCII fields with bounded arithmetic. The STM32 owns all
// thresholds. The ESP accepts a valid hit bit even within a hysteresis band.
bool number(const char *&p, uint32_t maximum, uint32_t &value) {
    if (*p < '0' || *p > '9') return false;
    value = 0;
    do {
        const uint32_t digit = *p++ - '0';
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    return true;
}

bool readSample(const char *p, bool &hit, int &angle, int &mm) {
    if (strncmp(p, "SCAN,", 5) != 0) return false;
    p += 5;
    uint32_t sequence, h, a, d;
    if (!number(p, UINT32_MAX, sequence) || *p++ != ',' ||
        !number(p, 1, h) || *p++ != ',' ||
        !number(p, 180, a) || *p++ != ',') return false;
    if (strcmp(p, "-1") == 0) {
        if (h != 0) return false;
        mm = -1;
    } else {
        if (!number(p, 65535, d) || *p != '\0' || (h && d == 0)) return false;
        mm = static_cast<int>(d);
    }
    hit = h != 0; angle = static_cast<int>(a);
    return true;
}

void receiveTask(void *) {
    Feed feed{};
    char line[64];
    size_t used = 0;
    bool discard = false;
    for (;;) {
        while (Serial2.available()) {
            const int c = Serial2.read();
            if (c < 0) break;
            if (c == '\r' || c == '\n') {
                if (!discard && used) {
                    line[used] = 0;
                    int angle, mm;
                    bool hit;
                    if (readSample(line, hit, angle, mm)) {
                        feed.received = true;
                        feed.lastFrameUs = esp_timer_get_time();
                        if (hit) {
                            // The sweep can see one fixed target on each pass.
                            // A hit following four seconds of silence gets display priority.
                            if (!feed.haveHit || feed.lastFrameUs - feed.lastHitUs >= 4 * usPerSecond)
                                ++feed.episode;
                            feed.lastHitUs = feed.lastFrameUs;
                            feed.haveHit = true;
                            feed.hitAngle = angle;
                            feed.hitMm = mm;
                            feed.hitUptimeS = feed.lastFrameUs / usPerSecond;
                        }
                        xQueueOverwrite(newestFeed, &feed);
                    }
                }
                used = 0;
                discard = false;
            } else if (c < 32 || c > 126 || used + 1 >= sizeof(line)) {
                discard = true;
            } else if (!discard) {
                line[used++] = static_cast<char>(c);
            }
        }
        vTaskDelay(1);
    }
}
}

void startDisplayApp() {
    Serial.begin(115200);
    Serial2.setRxBufferSize(2048);
    Serial2.begin(115200, SERIAL_8N1, 21, 22);
    newestFeed = xQueueCreate(1, sizeof(Feed));
    if (!newestFeed || xTaskCreate(receiveTask, "scan RX", 4096, nullptr, 2, nullptr) != pdPASS) {
        Serial.println("Cannot start STM32 receiver.");
        return;
    }
    panelStarted = startPanel();
    if (!panelStarted) Serial.println("Cannot allocate panel buffers.");
}

void updateDisplayApp() {
    delay(25);
    const int64_t now = esp_timer_get_time();
    if (!panelStarted || now < nextDrawUs) return;
    Feed feed{};
    xQueuePeek(newestFeed, &feed, 0);
    const LinkState link = !feed.received ? LinkState::Waiting :
        (now - feed.lastFrameUs < linkTimeout ? LinkState::Online : LinkState::Offline);
    const Screen candidate = {link, feed.haveHit, feed.hitAngle, feed.hitMm, feed.hitUptimeS};
    const bool event = !haveDisplayed || displayed.link != link || displayedEpisode != feed.episode;
    if (!event) {
        if (!feed.haveHit || (displayed.hitUptimeS == feed.hitUptimeS &&
             displayed.angle == feed.hitAngle && displayed.distanceMm == feed.hitMm)) return;
        if (now - lastDisplayUs < repeatGap) return;
    }

    const auto result = drawScreen(candidate, now >= nextFullUs);
    const int64_t finished = esp_timer_get_time();
    if (result == PanelUpdate::Failed) {
        Serial.println("Display refresh unconfirmed. Check ESP32-L P3, BUSY, and ribbon.");
        nextDrawUs = finished + fullGap;
        return;
    }
    if (result == PanelUpdate::WaitForFull) {
        nextDrawUs = nextFullUs;
        return;
    }
    displayed = candidate;
    displayedEpisode = feed.episode;
    haveDisplayed = true;
    lastDisplayUs = finished;
    nextDrawUs = finished + shortGap;
    if (result == PanelUpdate::Full) nextFullUs = finished + fullGap;
    Serial.printf("Display: link %d, previous hit %d, angle %d, distance %d mm.\n",
                  static_cast<int>(link), feed.haveHit, feed.hitAngle, feed.hitMm);
}

