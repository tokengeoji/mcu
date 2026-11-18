#include "protocol.h"
#include <stdlib.h>
#include <string.h>

static bool split(char *line, char type, char **fields, int count) {
    if (line[0] != type || line[1] != ',') return false;
    char *start = line + 2;
    for (int i = 0; i < count; ++i) {
        if (!*start) return false;
        fields[i] = start;
        char *comma = strchr(start, ',');
        if (i == count - 1) return comma == nullptr;
        if (!comma || comma == start) return false;
        *comma = 0;
        start = comma + 1;
    }
    return false;
}

static bool decimal(const char *s, uint32_t limit, uint32_t &value) {
    if (!*s) return false;
    uint64_t n = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        n = n * 10 + (*s++ - '0');
        if (n > limit) return false;
    }
    value = static_cast<uint32_t>(n);
    return true;
}

static bool coordinate(const char *s, int &value) {
    uint32_t magnitude;
    bool negative = *s == '-';
    if (!decimal(s + negative, 65000, magnitude)) return false;
    value = negative ? -static_cast<int>(magnitude) : static_cast<int>(magnitude);
    return true;
}

bool parsePosition(char *line, Position &result) {
    char *f[6];
    uint32_t corridor, valid;
    Position p = {};
    if (!split(line, 'P', f, 6) ||
        !decimal(f[0], UINT32_MAX, p.sequence) || !decimal(f[1], UINT32_MAX, p.sampledAt) ||
        !decimal(f[2], 4, corridor) || !coordinate(f[3], p.x) ||
        !coordinate(f[4], p.y) || !decimal(f[5], 1, valid)) return false;
    if ((valid && corridor == 0) || (!valid && (corridor || p.x || p.y))) return false;
    p.corridor = static_cast<int>(corridor);
    p.valid = valid != 0;
    result = p;
    return true;
}

static bool listed(const char *s, const char *const *choices) {
    for (; *choices; ++choices) if (strcmp(*choices, s) == 0) return true;
    return false;
}

bool parseStatus(char *line, Status &result) {
    static const char *const commands[] = {"NONE", "SAFE", "CAUTION", "DANGER", "ARRIVED", nullptr};
    static const char *const alarms[] = {"UNKNOWN", "CLEAR", "ACTIVE", "FAULT", "ACTIVE_FAULT", nullptr};
    static const char *const doors[] = {"UNSET", "CLOSED", "OPEN", nullptr};
    char *f[3];
    if (!split(line, 'S', f, 3) || !listed(f[0], commands) ||
        !listed(f[1], alarms) || !listed(f[2], doors)) return false;
    strcpy(result.command, f[0]);
    strcpy(result.alarm, f[1]);
    strcpy(result.door, f[2]);
    return true;
}
