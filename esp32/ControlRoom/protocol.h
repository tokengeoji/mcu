#pragma once

#include <stdint.h>

struct Position {
    uint32_t sequence, sampledAt;
    int corridor, x, y;
    bool valid;
};

struct Status {
    char command[8], alarm[13], door[7];
};

// Consumes the NUL-terminated line buffer. Rejects extra or empty fields.
bool parsePosition(char *line, Position &position);
bool parseStatus(char *line, Status &status);
