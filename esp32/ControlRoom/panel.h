#pragma once

#include "protocol.h"

struct Screen {
    int x, y;                  // pixel centre, x < 0 hides the position
    char location[64], state[72];
};

bool startPanel();
bool drawScreen(const Screen &screen);
bool locate(const Position &position, int &x, int &y);
