#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
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
static bool panelAvailable, panelStarted;
static Preferences panelPreferences;
static bool panelPreferencesAvailable, retainedHoldMap;
// Change the key when the default map artwork changes.
static constexpr char holdMapKey[] = "hold-foam-v6";
enum class DisplayMode { Auto, Hold, Live };
static DisplayMode mode = DisplayMode::Hold;
static bool redrawHold;
static const char *const modeNames[] = {"AUTO", "HOLD", "LIVE"};
static constexpr int64_t staleUs = 2500000; // 5 missed 500 ms heartbeats
static constexpr int64_t refreshIntervalUs = 180000000;

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

static bool invalidateRetainedMap() {
    if (!retainedHoldMap) return true;
    if (!panelPreferences.putBool(holdMapKey, false)) {
        Serial.println("EPD: could not clear retained map marker; redraw canceled.");
        return false;
    }
    retainedHoldMap = false;
    return true;
}

static void updateDisplay() {
    if (mode == DisplayMode::Hold && retainedHoldMap) return;
    static Screen previous;
    static bool displayed;
    static int64_t nextFullAt, retryAfter;
    const int64_t now = esp_timer_get_time();
    const bool fixed = mode != DisplayMode::Live;
    const bool automatic = mode == DisplayMode::Auto;
    const bool fullAllowed = now >= nextFullAt;
    if (now < retryAfter || (fixed && !fullAllowed)) return;
    Feed feed = {};
    xQueuePeek(newest, &feed, 0);
    Screen screen = {};
    screen.x = -1;
    if (fixed) {
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
    if (fixed) {
        strcpy(screen.state, automatic ? "MAP AUTO" : "MAP LOCKED");
    } else if (feed.haveStatus && now - feed.statusReceived < staleUs) {
        snprintf(screen.state, sizeof(screen.state), "CMD %s   S5 %s   DOOR %s",
                 feed.status.command, feed.status.alarm, feed.status.door);
    } else {
        strcpy(screen.state, "STM32 STATUS UNAVAILABLE");
    }
    if (displayed && !automatic && !redrawHold && !memcmp(&screen, &previous, sizeof(screen))) return;
    if (!panelStarted) {
        panelStarted = startPanel();
        if (!panelStarted) {
            panelAvailable = false;
            Serial.println("Display buffer allocation failed.");
            Serial2.print("ERROR\r\n");
            return;
        }
    }
    const PanelUpdate result = drawScreen(screen, fixed, fullAllowed);
    if (result == PanelUpdate::NeedsFull) {
        retryAfter = nextFullAt;
        return;
    }
    if (result == PanelUpdate::Failed) {
        retryAfter = esp_timer_get_time() + refreshIntervalUs;
        Serial.println("EPD: refresh unconfirmed; check power jumper P3 on ESP32-L. Retry in 180 s or restart.");
        Serial2.print("ERROR\r\n");
        displayed = false;
        return;
    }
    if (result == PanelUpdate::Full) nextFullAt = esp_timer_get_time() + refreshIntervalUs;
    if (!displayed) Serial2.print("READY\r\n");
    displayed = true;
    redrawHold = false;
    if (automatic) Serial.println("EPD: map auto; panel asleep. Next refresh in 180 s. USB A=auto, H=hold, U=live.");
    if (mode == DisplayMode::Hold) {
        if (panelPreferencesAvailable) {
            retainedHoldMap = panelPreferences.putBool(holdMapKey, true) != 0;
            if (!retainedHoldMap) Serial.println("EPD: could not save retained map marker.");
        }
        Serial.println("EPD: map locked; panel asleep. USB H=redraw after cooldown, A=auto, U=live.");
    }
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
    panelAvailable = true;
    panelPreferencesAvailable = panelPreferences.begin("epaper", false);
    if (!panelPreferencesAvailable) {
        Serial.println("EPD: retained map storage unavailable.");
    } else if (panelPreferences.getBool(holdMapKey, false)) {
        retainedHoldMap = true;
        Serial.println("EPD: retaining previous map without refreshing. USB H=redraw, A=auto, U=live.");
        Serial2.print("READY\r\n");
    }
}

void loop() {
    while (Serial.available()) {
        const int key = tolower(Serial.read());
        if ((key == 'a' || key == 'u' || key == 'h') && !invalidateRetainedMap()) continue;
        if (key == 'a') { mode = DisplayMode::Auto; redrawHold = false; }
        if (key == 'u') { mode = DisplayMode::Live; redrawHold = false; }
        if (key == 'h') {
            mode = DisplayMode::Hold;
            if (!redrawHold) Serial.println("EPD: HOLD redraw requested; full refreshes are at least 180 s apart.");
            redrawHold = true;
        }
        if (key == '?') {
            Serial.printf("EPD: mode=%s completed refreshes=%" PRIu32 " uptime=%lu ms reset=%d retained=%d\n",
                          modeNames[static_cast<unsigned>(mode)], panelRefreshCount(), millis(),
                          static_cast<int>(esp_reset_reason()), retainedHoldMap);
        }
    }
    if (panelAvailable) updateDisplay();
    delay(100);
}
