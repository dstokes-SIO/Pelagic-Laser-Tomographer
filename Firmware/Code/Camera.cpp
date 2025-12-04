#include "Camera.h"

bool Camera::powerStatus = false;

#if defined(ENABLE_USAGE_TRACKING)
uint32_t Camera::numberOfPowerOns  = 0;
uint32_t Camera::uptimeSeconds     = 0;
uint32_t Camera::recentPowerOnTime = 0;
#endif
