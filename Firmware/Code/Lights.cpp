#include "Lights.h"

Adafruit_NeoPixel Lights::pixels(
    Lights::NUMBER_OF_NEOPIXELS,    // Number of LEDs.
    NEOPIXELS_PIN,                  // Pin to talk to LEDs.
    NEO_RGB + NEO_KHZ800 );         // Red-green-blue color and default 800Khz.
