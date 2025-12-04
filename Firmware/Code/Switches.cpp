#include "Switches.h"

uint32_t Switches::lastDebounceMillis = 0;
uint16_t Switches::count = 0;
uint16_t Switches::previousSteadyState = 0;
uint16_t Switches::lastSteadyState = 0;
uint16_t Switches::lastFlickerableState = 0;
