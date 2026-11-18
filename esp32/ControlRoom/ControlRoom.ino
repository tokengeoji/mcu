#include <Arduino.h>
#include <esp_timer.h>
#include <inttypes.h>
#include "protocol.h"
#include "panel.h"

struct Feed {
    Position position;
    Status status;
    bool havePosition, haveStatus, haveFix;
    int64_t positionReceived, statusReceived, lastFixReceived; // ESP uptime in microseconds
};

static QueueHandle_t newest;
static bool panelAvailable;
static bool holdMap = true;
static constexpr int64_t staleUs = 2500000; // 5 missed 500 ms heartbeats

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
                    const int64_t now = esp_timer_get_time();
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
    static bool showingHeldMap;
    if (holdMap && showingHeldMap) return;
    Feed feed = {};
    xQueuePeek(newest, &feed, 0);
    const int64_t now = esp_timer_get_time();
    Screen screen = {};
    screen.x = -1;
    if (holdMap) {
        strcpy(screen.location, "CONTROL ROOM");
    } else if (feed.havePosition && now - feed.positionReceived < staleUs && feed.position.valid) {
        if (locate(feed.position, screen.x, screen.y)) {
            snprintf(screen.location, sizeof(screen.location), "POSITION  S%d   X %d mm   Y %d mm",
                     feed.position.corridor, feed.position.x, feed.position.y);
        } else {
            snprintf(screen.location, sizeof(screen.location), "CHECK MAP   S%d   X %d mm   Y %d mm",
                     feed.position.corridor, feed.position.x, feed.position.y);
        }
    } else if (feed.haveFix) {
        // Coarse age steps avoid driving a slow panel once per second while offline.
        const int64_t seconds = ((now - feed.lastFixReceived) / 10000000) * 10;
        snprintf(screen.location, sizeof(screen.location), "POSITION UNKNOWN   LAST FIX >= %" PRId64 " s AGO", seconds);
    } else {
        strcpy(screen.location, "POSITION UNKNOWN   NO FIX");
    }
    if (holdMap) {
        strcpy(screen.state, "MAP LOCKED");
    } else if (feed.haveStatus && now - feed.statusReceived < staleUs) {
        snprintf(screen.state, sizeof(screen.state), "CMD %s   S5 %s   DOOR %s",
                 feed.status.command, feed.status.alarm, feed.status.door);
    } else {
        strcpy(screen.state, "STM32 STATUS UNAVAILABLE");
    }
    if (displayed && !memcmp(&screen, &previous, sizeof(screen))) return;
    if (!drawScreen(screen, holdMap)) {
        Serial.println("EPD: refresh unconfirmed; check power jumper P3 on ESP32-L. Retry in 180 s or restart.");
        Serial2.print("ERROR\r\n");
        displayed = false;
        showingHeldMap = false;
        delay(180000);  // Good Display tri-color refresh interval after an unconfirmed update.
        return;
    }
    if (!displayed) Serial2.print("READY\r\n");
    displayed = true;
    showingHeldMap = holdMap;
    if (holdMap) Serial.println("EPD: map locked; panel asleep. USB U=live, H=hold.");
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
    if (!panelAvailable) {
        Serial.println("Display buffer allocation failed.");
        Serial2.print("ERROR\r\n");
    }
}

void loop() {
    while (Serial.available()) {
        const int key = Serial.read();
        if (key == 'U' || key == 'u') holdMap = false;
        if (key == 'H' || key == 'h') holdMap = true;
        if (key == '?') Serial.printf("EPD: mode=%s completed refreshes=%" PRIu32 "\n",
                                      holdMap ? "HOLD" : "LIVE", panelRefreshCount());
    }
    if (panelAvailable) updateDisplay();
    delay(100);
}
