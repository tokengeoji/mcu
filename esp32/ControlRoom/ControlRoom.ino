#include <Arduino.h>
#include "protocol.h"
#include "panel.h"

struct Feed {
    Position position;
    Status status;
    bool havePosition, haveStatus, haveFix;
    uint32_t positionReceived, statusReceived, lastFixReceived;
};

static QueueHandle_t newest;
static bool panelAvailable;
static constexpr uint32_t staleMs = 2500; // 5 missed 500 ms heartbeats

static void readLink(void *) {
    Feed feed = {};
    char line[96];
    size_t used = 0;
    bool discard = false;
    for (;;) {
        while (Serial2.available()) {
            const int ch = Serial2.read();
            if (ch < 0) break;
            if (ch == '\r' || ch == '\n') {
                if (used && !discard) {
                    line[used] = 0;
                    const uint32_t now = millis();
                    if (line[0] == 'P' && parsePosition(line, feed.position)) {
                        if (!feed.havePosition) Serial.println("UART: first valid STM32 position frame.");
                        feed.havePosition = true;
                        feed.positionReceived = now;
                        if (feed.position.valid) {
                            feed.haveFix = true;
                            feed.lastFixReceived = now;
                        }
                        xQueueOverwrite(newest, &feed);
                    } else if (line[0] == 'S' && parseStatus(line, feed.status)) {
                        if (!feed.haveStatus) Serial.println("UART: first valid STM32 status frame.");
                        feed.haveStatus = true;
                        feed.statusReceived = now;
                        xQueueOverwrite(newest, &feed);
                    }
                }
                used = 0;
                discard = false;
            } else if (ch < 32 || ch > 126 || used + 1 >= sizeof(line)) {
                discard = true;
            } else if (!discard) {
                line[used++] = static_cast<char>(ch);
            }
        }
        vTaskDelay(1);
    }
}

static void updateDisplay() {
    static Screen previous;
    static bool displayed;
    Feed feed = {};
    xQueuePeek(newest, &feed, 0);
    const uint32_t now = millis();
    Screen screen = {};
    screen.x = -1;
    if (feed.havePosition && now - feed.positionReceived < staleMs && feed.position.valid) {
        if (locate(feed.position, screen.x, screen.y)) {
            snprintf(screen.location, sizeof(screen.location), "POSITION  S%d   X %d mm   Y %d mm",
                     feed.position.corridor, feed.position.x, feed.position.y);
        } else {
            snprintf(screen.location, sizeof(screen.location), "CHECK MAP   S%d   X %d mm   Y %d mm",
                     feed.position.corridor, feed.position.x, feed.position.y);
        }
    } else if (feed.haveFix) {
        // Coarse age steps avoid driving a slow panel once per second while offline.
        const unsigned long seconds = ((now - feed.lastFixReceived) / 10000UL) * 10UL;
        snprintf(screen.location, sizeof(screen.location), "POSITION UNKNOWN   LAST FIX >= %lu s AGO", seconds);
    } else {
        strcpy(screen.location, "POSITION UNKNOWN   NO FIX");
    }
    if (feed.haveStatus && now - feed.statusReceived < staleMs) {
        snprintf(screen.state, sizeof(screen.state), "CMD %s   S5 %s   DOOR %s",
                 feed.status.command, feed.status.alarm, feed.status.door);
    } else {
        strcpy(screen.state, "STM32 STATUS UNAVAILABLE");
    }
    if (displayed && !memcmp(&screen, &previous, sizeof(screen))) return;
    if (!drawScreen(screen)) {
        Serial.println("EPD: refresh unconfirmed; check power jumper P3 on ESP32-L. Retry in 180 s or restart.");
        Serial2.print("ERROR\r\n");
        displayed = false;
        delay(180000);  // Good Display tri-color refresh interval after an unconfirmed update.
        return;
    }
    displayed = true;
    previous = screen;
}

void setup() {
    Serial.begin(115200);
    Serial2.setRxBufferSize(2048);
    // ESP32-L header P6: pin 9 (SDA/GPIO21) is RX; pin 10 (SCL/GPIO22) is TX.
    Serial2.begin(115200, SERIAL_8N1, 21, 22);
    newest = xQueueCreate(1, sizeof(Feed));
    if (!newest || xTaskCreate(readLink, "STM32 RX", 4096, nullptr, 2, nullptr) != pdPASS) {
        Serial.println("UART task allocation failed.");
        Serial2.print("ERROR\r\n");
        return;
    }
    panelAvailable = startPanel();
    if (panelAvailable) Serial2.print("READY\r\n");
    else {
        Serial.println("Display buffer allocation failed.");
        Serial2.print("ERROR\r\n");
    }
}

void loop() {
    if (panelAvailable) updateDisplay();
    delay(100);
}
