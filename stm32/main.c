/* Control and position rules from the supplied wiring plan, sections 07-08.
   The official hardware implementation is in the attached ST SPL V1 sources. */
#include "station.h"
#include "site_profile.h"
#include "System_func.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum Command { NONE, SAFE, CAUTION, DANGER, ARRIVED };
static const char *const commands[] = {"NONE","SAFE","CAUTION","DANGER","ARRIVED"};
static enum Command command;
static u8 door_opened, alarm_on;
static char held_key;
static u8 echo_beep;
static u32 beep_started;
#define ECHO_BEEP_MS 40U
enum Alarm { UNKNOWN, CLEAR, ACTIVE, FAULT, ACTIVE_FAULT };
static const char *const alarms[] = {"UNKNOWN","CLEAR","ACTIVE","FAULT","ACTIVE_FAULT"};
static enum Alarm alarm;
static int raw[SENSOR_COUNT] = {-1,-1,-1,-1,-1};
static u32 measured_at[SENSOR_COUNT], sequence, blink_start;
static u8 status_dirty = 1;

typedef struct { int corridor, x, y; u32 time; } Position;
static Position previous;
#define SWEEP_MS (SLOT_MS * 8U)

static int usable(int mm)
{
    return SITE_RANGE_MIN_MM > 0 && SITE_RANGE_MAX_MM >= SITE_RANGE_MIN_MM &&
           mm >= SITE_RANGE_MIN_MM && mm <= SITE_RANGE_MAX_MM;
}

static void hazard_reading(int mm)
{
    static int successive;
    enum Alarm before = alarm;
    int qualified;
    if (SITE_HAZARD_SAMPLES <= 0 || !usable(SITE_HAZARD_ON_MM) ||
        !usable(SITE_HAZARD_OFF_MM) || SITE_HAZARD_ON_MM >= SITE_HAZARD_OFF_MM) {
        alarm = UNKNOWN;
        return;
    }
    if (!usable(mm)) {
        successive = 0;
        alarm = alarm_on ? ACTIVE_FAULT : FAULT;
    } else {
        qualified = alarm_on ? mm >= SITE_HAZARD_OFF_MM : mm <= SITE_HAZARD_ON_MM;
        if (qualified) {
            if (++successive >= SITE_HAZARD_SAMPLES) {
                alarm_on = !alarm_on;
                successive = 0;
            }
        } else successive = 0;
        if (alarm_on) alarm = ACTIVE;
        else if (mm >= SITE_HAZARD_OFF_MM || before == CLEAR || before == ACTIVE) alarm = CLEAR;
        else if (before != FAULT) alarm = UNKNOWN;
    }
    if (before != alarm) status_dirty = 1;
}

static int candidate(u8 i, Position *point)
{
    const SiteCorridor *c = &site_corridors[i];
    int along;
    if (!usable(raw[i]) || c->length_mm <= 0 || c->length_mm > 32500 ||
        c->x_mm < -32500 || c->x_mm > 32500 || c->y_mm < -32500 || c->y_mm > 32500 ||
        c->offset_mm < -32500 || c->offset_mm > 32500 || !usable(c->background_mm) ||
        c->margin_mm <= 0 || c->margin_mm > c->background_mm ||
        raw[i] > c->background_mm - c->margin_mm) return 0;
    if (!(((c->dx == 1 || c->dx == -1) && c->dy == 0) ||
          ((c->dy == 1 || c->dy == -1) && c->dx == 0))) return 0;
    along = raw[i] + c->offset_mm;
    if (along < 0 || along > c->length_mm) return 0;
    point->corridor = i + 1;
    point->x = c->x_mm + c->dx * along;
    point->y = c->y_mm + c->dy * along;
    point->time = measured_at[i];
    return 1;
}

static Position locate(u32 now)
{
    Position selected = {0,0,0,0}, p;
    u8 i, count = 0;
    u32 elapsed, distance, allowed;
    int remember = previous.corridor;
    if (now - previous.time > SWEEP_MS * 2) remember = 0;
    selected.time = now;
    for (i = 0; i < 4; ++i) {
        if (now - measured_at[i] > SWEEP_MS || !candidate(i, &p)) continue;
        if (remember) {
            if (abs(p.corridor - previous.corridor) > 1) continue;
            elapsed = p.time - previous.time;
            if (SITE_MAX_SPEED_MM_S > 0 && SITE_MAX_SPEED_MM_S <= 65535 &&
                SITE_POSITION_ERROR_MM >= 0 && SITE_POSITION_ERROR_MM <= 32500) {
                distance = abs(p.x - previous.x) + abs(p.y - previous.y);
                allowed = elapsed * (u32)SITE_MAX_SPEED_MM_S / 1000 +
                          2UL * SITE_POSITION_ERROR_MM;
                if (distance > allowed) continue;
            }
        }
        selected = p;
        ++count;
    }
    if (count == 1) previous = selected;
    else {
        selected.corridor = selected.x = selected.y = 0;
        selected.time = now;
        if (!remember) previous.corridor = 0;
    }
    return selected;
}

static void send_position(u32 now)
{
    char line[80];
    Position p = locate(now);
    sprintf(line, "P,%lu,%lu,%d,%d,%d,%d\r\n", ++sequence, p.time,
            p.corridor, p.x, p.y, p.corridor != 0);
    serial_send(DISPLAY_PORT, line);
}

static void send_status(void)
{
    char line[64];
    const char *door = servo_ready() ? (door_opened ? "OPEN" : "CLOSED") : "UNSET";
    sprintf(line,"S,%s,%s,%s\r\n", commands[command], alarms[alarm], door);
    if (serial_send(DISPLAY_PORT, line)) status_dirty = 0;
}

static void operator_line(char *text, u32 now)
{
    int i;
    char out[80];
    if (strcmp(text, "RAW") == 0) {
        sprintf(out, "R,%d,%d,%d,%d,%d\r\n",raw[0],raw[1],raw[2],raw[3],raw[4]);
        serial_send(PC_PORT, out);
        return;
    }
    for (i = SAFE; i <= ARRIVED; ++i) {
        if (!strcmp(text, commands[i])) {
            command = (enum Command)i;
            blink_start = now;
            status_dirty = 1;
            serial_send(PC_PORT, "OK\r\n");
            return;
        }
    }
    serial_send(PC_PORT, "ERR COMMAND\r\n");
}

static void handle_serial(u32 now)
{
    char line[LINE_CAPACITY];
    if (serial_line(PC_PORT, line)) operator_line(line, now);
    if (serial_line(DISPLAY_PORT, line)) {
        if (!strcmp(line, "READY")) status_dirty = 1;
        else if (!strcmp(line, "ERROR")) serial_send(PC_PORT, "ERR DISPLAY\r\n");
        /* ACK is informational. Display silence never holds up acquisition. */
    }
}

static void outputs(u32 now)
{
    char key = keypad_key(now);
    u8 lit = ((now - blink_start) / 500U) % 2 == 0;
    leds_set(command == SAFE || command == ARRIVED ||
             ((command == CAUTION || command == DANGER) && lit),
             command == ARRIVED || (command == DANGER && lit));
    if (key == '0' && held_key != key && command == ARRIVED && servo_ready() && !door_opened) {
        servo_open();
        door_opened = 1;
        status_dirty = 1;
    }
    if (key != held_key || now - beep_started >= ECHO_BEEP_MS) echo_beep = 0;
    held_key = key;
    piezo_set(alarm_on || echo_beep);
}

static void sample(u32 now)
{
    static const u8 order[8] = {4,0,4,1,4,2,4,3};
    static u8 slot, waiting;
    static u32 started;
    static u8 first = 1;
    int mm;
    u8 sensor = order[slot];
    if (waiting) {
        if (!echo_result(now - started, &mm)) return;
        raw[sensor] = mm;
        measured_at[sensor] = now;
        if (held_key == '1' + sensor && mm > 0) {
            beep_started = now;
            echo_beep = 1;
        }
        if (sensor == 4) hazard_reading(mm);
        if (slot == 7) send_position(now);
        waiting = 0;
        slot = (slot + 1) & 7;
    } else if (first || now - started >= SLOT_MS) {
        started = now;
        first = 0;
        echo_start(sensor);
        waiting = 1;
    }
}

int main(void)
{
    u32 now, reported = 0;
    Init_STM32F103();
    station_hardware_init();
    for (;;) {
        now = station_ms;
        sample(now);
        handle_serial(now);
        outputs(now);
        if (status_dirty || now - reported >= 500) {
            send_status();
            reported = now;
        }
    }
}
