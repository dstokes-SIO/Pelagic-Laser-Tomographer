#pragma once
/**
 * @file
 * Define the microcontroller's pin usage.
 */

// Digital pins.
#define SDCARD_PIN                  4   // SD memory card chip select
#define LSM9DS1_MCS_PIN             5   // Inertia module.
#define LSM9DS1_XGCS_PIN            6   // Inertia module.
#define BOARD_GREEN_LED_PIN         8   // Built-in board green LED
#define CAMERA_SHUTTER_PIN          10  // Camera shutter relay
#define LASER_PIN                   11  // Laser on/off relay
#define LSM9DS1_MISO_PIN            12  // Inertia module.
#define BOARD_RED_LED_PIN           13  // Built-in board red LED
#define STARTSTOP_SWITCH_PIN        14  // Start/stop switch
#define CAMERA_POWER_SET_PIN        15  // Camera power relay set
#define INTENSIFIER_POWER_SET_PIN   17  // Intensifier power relay set
#define INTENSIFIER_POWER_UNSET_PIN 18  // Intensifier power relay unset
#define NEOPIXELS_PIN               19  // Multi-colored LED group

// Analog pins.
#define LSM9DS1_MOSI_PIN            A4  // Inertia module.
#define LSM9DS1_SCK_PIN             A5  // Inertia module.

// Only used if battery monitoring uses raw voltage. See Battery.h and
// BATTERY_USE_RAW_VOLTAGE for BATTERY_ENABLE_CONTROLLER_MONITORING and
// BATTERY_ENABLE_MAIN_MONITORING.
#define CONTROLLER_BATTERY_VOLTAGE_PIN  A7 // Controller battery raw voltage.
#define MAIN_BATTERY_VOLTAGE_PIN    A7  // Main battery raw voltage.
