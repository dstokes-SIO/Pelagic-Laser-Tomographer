#include "Laser.h"

bool Laser::powerStatus = false;

#if defined(ENABLE_USAGE_TRACKING)
uint32_t Laser::numberOfPowerOns = 0;
uint32_t Laser::uptimeSeconds = 0;
uint32_t Laser::recentPowerOnTime = 0;
#endif
