#include "Sensors.h"

Adafruit_LSM9DS1 Sensors::inertiaSensor;
MS5837 Sensors::pressureSensor;
TSYS01 Sensors::temperatureSensor;
uint8_t Sensors::initialized = 0;
