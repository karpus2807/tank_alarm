#pragma once

// Tank status is UDP JSON on TANK_UDP_PORT.
// The packet carries cm, pct, ok, and lvl. The alarm chooses the beep from pct and lvl.

#define TANK_UDP_PORT 5005
#define TANK_PACKET_MAX 512
