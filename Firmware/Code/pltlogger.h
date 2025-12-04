#pragma once

#define VERSION "Sept 23, 2021"


//----------------------------------------------------------------------
// Debug features.
//----------------------------------------------------------------------
// Debug-only. Define to enable device boot to wait until the serial
// port is ready before continuing. If there is no computer attached to
// the USB port, this will cause the boot to hang. But if there is,
// then boot messages will be printed to the port and visible by a
// terminal monitor on the port. This can be useful during boot debugging.
//#define DEBUG_SERIAL

// Debug-only. Define to enable benchmarking during the snap-and-log
// process. This reports to the serial output the milliseconds taken
// for each major step in capturing an image, including turning on
// the laser, tripping the shutter, turning off the laser, and logging
// the results.
//#define DEBUG_BENCHMARK_SNAP_AND_LOG

// Debug-only. Define to enable messages to the serial output on each
// major camera action, such as power on/off and shutter.
//#define DEBUG_VERBOSE_CAMERA

// Debug-only. Define to enable messages to the serial output on each
// major laser action, such as power on/off.
//#define DEBUG_VERBOSE_LASER

// Debug-only. Define to enable messages to the serial output on each
// major battery action, such as initialization and checking levels.
//#define DEBUG_VERBOSE_BATTERY

// Debug-only. Define to enable messages to the serial output on each
// major clock action, such as initialization and setting values.
//#define DEBUG_VERBOSE_CLOCK

// Debug-only. Define to enable messages to the serial output on each
// major lights action, such as initialization and turning them on/off.
//#define DEBUG_VERBOSE_LIGHTS

// Debug-only. Define to enable messages to the serial output on each
// major sensors action, such as initialization and reading values.
//#define DEBUG_VERBOSE_SENSORS

// Debug-only. Define to enable messages to the serial output on each
// major switches action, such as initialization, press, and release.
//#define DEBUG_VERBOSE_SWITCHES

// Debug-only. Define to convert critical errors for missing batteries into
// warnings only. This should not be defined in production, but when debugging
// this allows the device to try to operate even when it cannot find its
// batteries.
#define DEBUG_BATTERY_MISSING_IS_WARNING

// Debug-only. Define to convert critical errors for missing sensors into
// warnings only. This should not be defined in production, but when debugging
// this allows the device to try t operate even when it cannot find its
// sensors.
#define DEBUG_SENSORS_MISSING_IS_WARNING

// Debug-only. Define to convert critical errors for missing clock into
// warnings only. This should not be defined in production, but when debugging
// this allows the device to try t operate even when it cannot find its
// clock.
#define DEBUG_CLOCK_MISSING_IS_WARNING


//----------------------------------------------------------------------
// Software features.
//----------------------------------------------------------------------
// Optional but recommended. Define to enable periodic battery level
// checks. When the battery level drops below a warning level (see
// BATTERY_WARN_PERCENT) the device sets the hardware state to a warning.
// When the level drops below the error level (see BATTERY_ERROR_PERCENT),
// the device sets the hardware state to a error and stops snap-and-log. The
// periodic check (see BATTERY_CHECK_INTERVAL) should be every minute or
// so. It takes a little time, which could affect snap-and-log interval
// accuracy.
//#define ENABLE_BATTERY_CHECK

#if defined(ENABLE_BATTERY_CHECK)
#define BATTERY_CHECK_INTERVAL  60000 // ms
#endif

#define BATTERY_ERROR_PERCENT   10.0
#define BATTERY_WARN_PERCENT    20.0

// Optional. Define to enable usage tracking. This enables updates to
// counters and uptime for the laser, camera, and device as a whole.
// It also enables periodic updates of a usage stats file on the SD card.
// The values can be used to track usage and uptime for a particular
// battery size and keep track of images recorded to a camera's SD card.
// The counter and uptime updates and the periodic file update (see
// USAGE_FILE_UPDATE_INTERVAL_EVENTS) take a little time, which could affect
// snap-and-log interval accuracy.
//#define ENABLE_USAGE_TRACKING

#if defined(ENABLE_USAGE_TRACKING)
#define USAGE_FILE_UPDATE_INTERVAL_EVENTS 60 // events

typedef struct Usage
{
    // Persistent usage counters and uptime. These are saved to the
    // usage tracking file periodically.
    uint32_t numberOfBoots;
    uint32_t numberOfCameraBoots;
    uint32_t numberOfLaserBoots;
    uint32_t numberOfEventsLogged;
    uint32_t numberOfImagesSnapped;
    uint32_t controllerUptimeSeconds;
    uint32_t cameraUptimeSeconds;
    uint32_t laserUptimeSeconds;

    // Time stamps as seconds since boot. Used to calculate when
    // to update the tracking file.
    uint32_t recentUpdateTime;
} Usage;

extern Usage usage;
#endif


//----------------------------------------------------------------------
// Default settings.
//----------------------------------------------------------------------
#define DEFAULT_FRAME_INTERVAL   1000 // ms
#define DEFAULT_BURST_SIZE       1
#define DEFAULT_LASER_CONTINUOUS false


//----------------------------------------------------------------------
// Intervals and limits.
//----------------------------------------------------------------------
// Shortest frame interval.
//   There are delays built into several of the steps involved in snapping
//   a photo and writing a log file entry. Benchmarking finds these to be
//   around 200 ms. This determines the fastest log time.
#define MINIMUM_FRAME_INTERVAL 200  // ms


//----------------------------------------------------------------------
// Status values.
//----------------------------------------------------------------------
// Hardware status used by getHardwareStatus().
#define HARDWARE_OFF      0
#define HARDWARE_BOOTING  1
#define HARDWARE_ERRORS   2
#define HARDWARE_WARNINGS 3
#define HARDWARE_READY    4

// Software status used by getSoftwareStatus().
#define SOFTWARE_OFF      0
#define SOFTWARE_BOOTING  1
#define SOFTWARE_ERRORS   2
#define SOFTWARE_READY    3
#define SOFTWARE_RUNNING  4

// Camera status used by getCameraStatus().
#define CAMERA_OFF        0
#define CAMERA_BOOTING    1
#define CAMERA_READY      2
#define CAMERA_SHOOTING   3


//----------------------------------------------------------------------
// Forward define functions available to all classes.
//----------------------------------------------------------------------
extern void reset( );

extern uint8_t getHardwareStatus( );
extern void setHardwareStatus( const uint8_t );

extern uint8_t getSoftwareStatus( );
extern void setSoftwareStatus( const uint8_t );

extern uint8_t getCameraStatus( );
extern void setCameraStatus( const uint8_t );

extern bool isLaserContinuous( );
extern bool setLaserContinuous( const bool );

extern uint8_t getBurstSize( );
extern bool setBurstSize( const uint8_t );

extern bool startRunning( );
extern bool stopRunning( );

extern bool snapAndLog( const uint8_t );
extern uint32_t getFrameInterval( );
extern bool setFrameInterval( const uint32_t );
