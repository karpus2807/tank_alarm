#pragma once

// UDP JSON payload from tank → alarm (also mirrored in Serial).
// Example: {"id":1,"cm":23.4,"pct":78,"alarm":0,"ok":1}

#define TANK_UDP_PORT 5005
#define TANK_PACKET_MAX 220

#define ALARM_LEVEL_NONE 0
#define ALARM_LEVEL_WARN 1
#define ALARM_LEVEL_FULL 2
