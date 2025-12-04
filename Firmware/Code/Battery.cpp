#include "Battery.h"

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
Adafruit_LC709203F Battery::mainBattery;
#endif

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
Adafruit_LC709203F Battery::controllerBattery;
#endif

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F && \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
uint8_t Battery::currentMuxDevice = 255;
#endif

bool Battery::mainInitialized = false;
bool Battery::controllerInitialized = false;
