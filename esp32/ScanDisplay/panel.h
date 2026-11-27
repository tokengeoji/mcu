#pragma once
#include <stdint.h>

enum class LinkState { Waiting, Online, Offline };

struct Screen {
    LinkState link;
    bool haveHit;
    int angle;
    int distanceMm;
    uint64_t hitUptimeS;  // Reception time since this ESP32 boot, not a wall clock.
};

enum class PanelUpdate { Done, Full, WaitForFull, Failed };
bool startPanel();
PanelUpdate drawScreen(const Screen &screen, bool fullAllowed);
