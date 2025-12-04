/**
 * Pelagic Laser Tomographer (PLT) spatial logger.
 *
 * Developed by the Scripps Institution of Oceanography and the
 * San Diego Supercomputer Center at the University of California at
 * San Diego.
 *
 * The PLT device combines a laser to illuminate pelagic particulate,
 * and a digital camera with intensifier to photograph that particulate
 * at regular time intervals as the device is lowered through a water column.
 * Additional sensors track pressure, temperature, and orientation, and
 * LEDs report basic device status. The device is controlled by an Arduino
 * microcontroller with an SD card used to log the time and sensor data at
 * which each photograph is taken. The camera has its own SD card to store
 * images. A USB serial port on the microcontroller supports a set of basic
 * commands to query the device's status and adjust settings.
 *
 * This software, when loaded onto the microcontroller, initializes the
 * device, provides serial port commands, and runs the camera, laser,
 * LEDs, and logging.
 *
 * See HARDWARE.txt for hardware requirements.
 * See LIBRARIES.txt for software library requirements.
 * See CHANGES.txt for a change log.
 * See TODO.txt for a to-do list.
 */
#include <Arduino.h>
#include "pltlogger.h"

#include "pins.h"       // Device pins.
#include "Battery.h"    // Camera and intensifier battery.
#include "Camera.h"     // Camera and intensifier.
#include "Clock.h"      // Real time clock.
#include "FileSystem.h" // SDCard file system.
#include "Laser.h"      // Laser.
#include "Lights.h"     // Light (LEDs).
#include "Sensors.h"    // Inertial, pressure, and temperature sensors.
#include "Switches.h"   // Switches.
#include "Commands.h"   // Serial port commands.


//----------------------------------------------------------------------
// Globals.
//----------------------------------------------------------------------
// Settings.
bool laserContinuous   = DEFAULT_LASER_CONTINUOUS;
uint8_t burstSize      = DEFAULT_BURST_SIZE;
uint32_t frameInterval = DEFAULT_FRAME_INTERVAL;

// Current state.
uint8_t hardwareStatus   = HARDWARE_OFF;
uint8_t softwareStatus   = SOFTWARE_OFF;
uint8_t cameraStatus     = CAMERA_OFF;
bool batteriesPresent    = false;
uint32_t previousLogTime = 0;

#if defined(ENABLE_BATTERY_CHECK)
// Battery checking state.
#define BATTERY_OK       0
#define BATTERY_LOW      1
#define BATTERY_CRITICAL 2
uint32_t previousBatteryCheckTime = 0;
uint8_t mainBatteryState = BATTERY_OK;
uint8_t controllerBatteryState = BATTERY_OK;
#endif

#if defined(ENABLE_USAGE_TRACKING)
// Usage tracking.
Usage usage;
#endif





//----------------------------------------------------------------------
// Settings.
//----------------------------------------------------------------------
/**
 * Returns the current burst size.
 *
 * The burst size is the number of images to shoot on each snap.
 *
 * @return
 *   Returns the size.
 *
 * @see setBurstSize()
 */
uint8_t getBurstSize( )
{
    return burstSize;
}

/**
 * Returns the current frame interval.
 *
 * @return
 *   Returns the frame interval, in ms.
 *
 * @see setFrameInterval()
 * @see snapAndLog()
 */
uint32_t getFrameInterval( )
{
    return frameInterval;
}

/**
 * Returns true if the laser should be on throughout a run.
 *
 * @return
 *   Returns true if continuous laser.
 *
 * @see setLaserContinuous()
 */
bool isLaserContinuous( )
{
    return laserContinuous;
}

/**
 * Sets the current burst size.
 *
 * The burst size is the number of images to shoot on each snap.
 *
 * @param[in] nImages
 *   Sets the size.
 *
 * @return
 *   Returns true if the change is accepted.
 *
 * @see getBurstSize()
 * @see FileSystem::saveSettings()
 */
bool setBurstSize( const uint8_t nImages )
{
    if ( nImages == burstSize )
        return true; // No change.

    if ( nImages == 0 )
        burstSize = DEFAULT_BURST_SIZE;
    else
        burstSize = nImages;
    FileSystem::saveSettings( frameInterval, laserContinuous, burstSize );
    return true;
}

/**
 * Sets the current frame interval.
 *
 * @param[in] interval
 *   The new frame interval, in ms.
 *
 * @return
 *   Returns true if the change is accepted.
 *
 * @see getFrameInterval()
 * @see snapAndLog()
 * @see FileSystem::saveSettings()
 */
bool setFrameInterval( const uint32_t interval )
{
    if ( interval == frameInterval )
        return true; // No change.

    if ( interval == 0 )
        frameInterval = DEFAULT_FRAME_INTERVAL;
    else if ( interval < MINIMUM_FRAME_INTERVAL )
        return false; // Too small.
    else
        frameInterval = interval;
    FileSystem::saveSettings( frameInterval, laserContinuous, burstSize );
    return true;
}

/**
 * Sets whether the laser should be on continuously or per shoot.
 *
 * @param[in] onOff
 *   Sets whether the laser should be on continuously (true).
 *
 * @return
 *   Returns true if the change is accepted.
 *
 * @see isLaserContinuous()
 * @see FileSystem::saveSettings()
 */
bool setLaserContinuous( const bool onOff )
{
    if ( onOff == laserContinuous )
        return true; // No change.
    laserContinuous = onOff;
    FileSystem::saveSettings( frameInterval, laserContinuous, burstSize );
    return true;
}





//----------------------------------------------------------------------
// Status.
//----------------------------------------------------------------------
/**
 * Returns the camera status.
 *
 * @return
 *   Returns the status.
 *
 * @see setCameraStatus()
 */
uint8_t getCameraStatus( )
{
    return cameraStatus;
}

/**
 * Returns the hardware status.
 *
 * @return
 *   Returns the status.
 *
 * @see setHardwareStatus()
 */
uint8_t getHardwareStatus( )
{
    return hardwareStatus;
}

/**
 * Returns the software status.
 *
 * @return
 *   Returns the status.
 *
 * @see setSoftwareStatus()
 * @see startRunning()
 * @see stopRunning()
 */
uint8_t getSoftwareStatus( )
{
    return softwareStatus;
}

/**
 * Sets the camera status.
 *
 * @param[in] status
 *   The camera status.
 *
 * @see getCameraStatus()
 */
void setCameraStatus( const uint8_t status )
{
    cameraStatus = status;
    Lights::setLightsForStatus( );
}

/**
 * Sets the hardware status.
 *
 * @param[in] status
 *   The hardware status.
 *
 * @see getHardwareStatus()
 */
void setHardwareStatus( const uint8_t status )
{
    hardwareStatus = status;
    Lights::setLightsForStatus( );
}

/**
 * Sets the software status.
 *
 * @param[in] status
 *   The software status.
 *
 * @see getSoftwareStatus()
 * @see startRunning()
 * @see stopRunning()
 */
void setSoftwareStatus( const uint8_t status )
{
    softwareStatus = status;
    Lights::setLightsForStatus( );
}





//----------------------------------------------------------------------
// Reset.
//----------------------------------------------------------------------
#if defined(ENABLE_USAGE_TRACKING)
/**
 * Resets usage tracking.
 *
 * All counters and uptimes are set to zero.
 *
 * @see FileSystem::loadUsage()
 * @see FileSystem::saveUsage()
 */
void resetUsage( )
{
    // Reset all counters.
    usage.numberOfBoots = 0;
    usage.numberOfCameraBoots = 0;
    usage.numberOfLaserBoots = 0;
    usage.numberOfEventsLogged = 0;
    usage.numberOfImagesSnapped = 0;

    // Reset all uptimes.
    usage.controllerUptimeSeconds = 0;
    usage.cameraUptimeSeconds = 0;
    usage.laserUptimeSeconds = 0;

    // Reset the reference timestamps.
    usage.recentUpdateTime = millis( ) / 1000;
}
#endif

/**
 * Resets settings and usage tracking.
 *
 * @see resetUsage()
 */
void reset( )
{
    // Stop running. If running, this should turn off the laser and
    // camera, close the data log, and reset the lights.
    if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
        stopRunning( );
    else
        Serial.print( "Stopped.\r\n" );

    Serial.print( "Device:\r\n" );
    FileSystem::closeDataLog( );
    Serial.print( "  Data log closed.\r\n");

    Laser::setPower( false );
    Serial.print( "  Laser off.\r\n");

    Camera::setPower( false, true );
    Serial.print( "  Camera and intensifier off.\r\n");

    Lights::reset( );
    Serial.print( "  Lights reset.\r\n");

    Serial.print( "Settings:\r\n" );
    setBurstSize( DEFAULT_BURST_SIZE );
    Serial.printf( "  Burst size reset to %d.\r\n", getBurstSize( ) );

    setFrameInterval( DEFAULT_FRAME_INTERVAL );
    Serial.printf( "  Image interval reset to %ld ms.\r\n", getFrameInterval( ) );

    setLaserContinuous( DEFAULT_LASER_CONTINUOUS );
    Serial.printf( "  Laser reset to %s.\r\n",
        isLaserContinuous( ) ? "continuous" : "normal" );

#if defined(ENABLE_USAGE_TRACKING)
    Serial.print( "Usage tracking reset.\r\n" );
    resetUsage( );
    FileSystem::saveUsage( usage );
#endif

    FileSystem::saveSettings( frameInterval, laserContinuous, burstSize );
}





//----------------------------------------------------------------------
// Run, snap, and log.
//----------------------------------------------------------------------
/**
 * Snaps an image, reads sensors, and adds a data log entry.
 *
 * If the laser mode is not continuous, the laser is turned on before
 * an image is captured, and off afterwards.
 *
 * The camera is triggered and sensors read, then added to the data log.
 *
 * @param[in] nImages
 *   The number of images in a burst.
 *
 * @return
 *   Returns true on success, false on failure. On failure, error
 *   messages have already been printed.
 *
 * @see getFrameInterval()
 * @see setFrameInterval()
 * @see startRunning()
 * @see stopRunning()
 */
bool snapAndLog( const uint8_t nImages )
{
    float pressure = 0.0;
    float depth = 0.0;
    float waterTemperature = 0.0;
    float deviceTemperature = 0.0;
    float accel[3];
    float mag[3];
    float gyro[3];
    bool status = true;

#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
    uint32_t previousTime = millis( );
    uint32_t nextTime = previousTime;
    uint32_t powerOnTime = 0;
    uint32_t powerOffTime = 0;
    uint32_t shutterTime = 0;
    uint32_t sensorTime = 0;
    uint32_t logTime = 0;
#endif

    //
    // Camera, intensifier, and laser power up (as needed).
    //
    // Insure the camera and intensifier power is on.
    // - When this function is called during a run, the camera and intensifier
    //   are already on.
    // - When this function is called outside of a run to snap just once, the
    //   camera and intensifier are not already on and must be turned on now.
    const bool initialCameraPower = Camera::isPowerOn( );
    if ( !initialCameraPower )
    {
        setCameraStatus( CAMERA_BOOTING );
        Camera::setPower( true );
        setCameraStatus( CAMERA_READY );
    }

    // Insure the laser is on.
    const bool initialLaserPower = Laser::isPowerOn( );
    if ( !initialLaserPower )
        Laser::setPower( true );

#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
    powerOnTime = (nextTime = millis()) - previousTime;
    previousTime = nextTime;
#endif

    //
    // Capture.
    //
    // Snap images and update usage.
    setCameraStatus( CAMERA_SHOOTING );
    Camera::snap( nImages );
#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
    shutterTime = (nextTime = millis()) - previousTime;
    previousTime = nextTime;
#endif

    //
    // Camera, intensifier, and laser power down (as needed).
    //
    // Turn off the laser, if it was originally off.
    if ( !initialLaserPower )
        Laser::setPower( false );

    // Turn off the camera and intensifier, if it was originally off.
    if ( !initialCameraPower )
    {
        Camera::setPower( false );
        setCameraStatus( CAMERA_OFF );
    }
    else
        setCameraStatus( CAMERA_READY );

#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
    powerOffTime = (nextTime = millis()) - previousTime;
    previousTime = nextTime;
#endif


#if defined(ENABLE_USAGE_TRACKING)
    //
    // Update usage tracking in memory.
    //
    usage.numberOfImagesSnapped += nImages;
    ++usage.numberOfEventsLogged;
    const uint32_t ut = millis( ) / 1000;
    usage.controllerUptimeSeconds += (ut - usage.recentUpdateTime);
    usage.recentUpdateTime = ut;
#endif


    //
    // Log.
    //
    // If there is a data log (and there is while running automaticaly),
    // then read the sensors and add a data log entry.
    //
    // If it is time to update the usage tracking file, get the most recent
    // values and save them.
    if ( FileSystem::isDataLogOpen( ) )
    {
        // Read the sensors.
        Sensors::getWaterPressure( pressure, depth );
        Sensors::getWaterTemperature( waterTemperature );
        Sensors::getInertia( accel, mag, gyro, deviceTemperature );
#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
        sensorTime = (nextTime = millis()) - previousTime;
        previousTime = nextTime;
#endif

        // Write to the data log.
        if ( !FileSystem::writeDataLog(
            Clock::nowString( ),
            Clock::nowMillisOffset( ),
            pressure,
            depth,
            waterTemperature,
            deviceTemperature,
            accel,
            mag,
            gyro ) )
        {
            // Log file write error. Possible failures:
            // - The SD card is not inserted.
            // - The SD card is full.
            // - The file has reached the 4GB max size for FAT.
            // - A hardware error has occurred.
            // - An internal SdFat error has occurred.
            Serial.print( "Cannot write to data log file.\r\n" );
            FileSystem::printErrorMessage( );

            if ( FileSystem::isCardPresent( ) )
            {
                // Try to log a status message. This will fail silently if the
                // SD card is full.
                FileSystem::writeStatus( "Cannot write to data log file" );
                FileSystem::writeStatus( FileSystem::getErrorMessage( ) );
            }
            status = false;
        }
#if defined(ENABLE_USAGE_TRACKING)
        else
        {
            // Update the usage tracking file.
            if ( USAGE_FILE_UPDATE_INTERVAL_EVENTS <= 1 ||
                (usage.numberOfEventsLogged % USAGE_FILE_UPDATE_INTERVAL_EVENTS) == 0 )
            {
                // Copy out the current Camera and Laser counters.
                usage.numberOfCameraBoots = Camera::getNumberOfPowerOns( );
                usage.cameraUptimeSeconds = Camera::getUptimeSeconds( );
                usage.numberOfLaserBoots  = Laser::getNumberOfPowerOns( );
                usage.laserUptimeSeconds  = Laser::getUptimeSeconds( );
                FileSystem::saveUsage( usage );
            }
        }
#endif

#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
        logTime = (nextTime = millis()) - previousTime;
        previousTime = nextTime;
#endif
    }

#ifdef DEBUG_BENCHMARK_SNAP_AND_LOG
    Serial.printf( "On %ld |Image %ld |Off %ld |Sensor %ld |Log %ld|= %ld\r\n",
        powerOnTime,
        shutterTime,
        powerOffTime,
        sensorTime,
        logTime,
        (powerOnTime + shutterTime + powerOffTime + sensorTime + logTime) );
#endif
    return status;
}

/**
 * Starts snapping images and logging sensor readings.
 *
 * If the current state is not READY_STATE, no action is taken.
 *
 * The camera and intensifier power is turned on. The lights are set
 * to show the device is running. A new log file is created. A starting
 * photo and sensor reading is logged and the current time noted for
 * tracking frame intervals.
 *
 * There are several delays built in to these steps, so this function
 * does not return quickly.
 *
 * @return
 *   Returns true on success, false on failure. On failure, error
 *   messages have already been printed.
 *
 * @see getCameraStatus()
 * @see getHardwareStatus()
 * @see getSoftwareStatus()
 * @see snapAndLog()
 * @see stopRunning()
 */
bool startRunning( )
{
    if ( getSoftwareStatus( ) != SOFTWARE_READY )
        return false;

    Serial.printf( "Starting...\r\n" );
    if ( !FileSystem::newDataLog( ) )
    {
        // Fail to create a new log. Possible failures:
        // - The SD card is not inserted.
        // - The SD card is full.
        // - The maximum number of files in a FAT directory has been reached.
        // - The maximum number of log files has been reached.
        //
        // FileSystem error codes have been set, so print an error message.
        Serial.print( "Cannot create new data log file.\r\n" );
        FileSystem::printErrorMessage( );

        if ( !FileSystem::isCardPresent( ) )
        {
            // Fail with missing SD card. There is no way to log a status
            // message. Go to an ERROR run state.
            setHardwareStatus( HARDWARE_ERRORS );
            setSoftwareStatus( SOFTWARE_ERRORS );
        }
        else
        {
            // Try to log a status message. This will fail silently if the
            // SD card is full.
            FileSystem::writeStatus( "** Cannot create new data log file" );
            FileSystem::writeStatus( FileSystem::getErrorMessage( ) );
        }
        return false;
    }

    // Announce and add a status message.
    char buf[1025];
    const char*const name = FileSystem::getDataLogFilename( );
    Serial.print( "Camera and intensifier powering up...\r\n" );
    sprintf( buf, "Start running. Logging to %s.", name );
    FileSystem::writeStatus( buf );

    // Turn on the camera. Leave it on while running.
    setSoftwareStatus( SOFTWARE_RUNNING );
    setCameraStatus( CAMERA_BOOTING );
    Camera::setPower( true );
    setCameraStatus( CAMERA_READY );
    Serial.printf( "Running. Logging to %s.\r\n", name );

    // If the laser mode is continuous, turn on the laser and leave it on.
    if ( isLaserContinuous( ) )
        Laser::setPower( true );

    // Initial shot.
    if ( !snapAndLog( burstSize ) )
    {
        // Write error on the log. Possible failures:
        // - The SD card is not inserted.
        // - The SD card is full.
        // - The file has reached the 4GB max size for FAT.
        // - A hardware error has occurred.
        // - An internal SdFat error has occurred.
        Camera::setPower( false );
        Laser::setPower( false );
        setCameraStatus( CAMERA_OFF );
        setSoftwareStatus( SOFTWARE_ERRORS );

        if ( !FileSystem::isCardPresent( ) )
        {
            Serial.print( "Cannot start running due to critical problems.\r\n" );
            setHardwareStatus( HARDWARE_ERRORS );
        }
        return false;
    }

    previousLogTime = millis( );
    Serial.println( );
    return true;
}

/**
 * Stops snapping photos and logging sensor readings.
 *
 * If the current state is not RUNNING_STATE, no action is taken.
 *
 * The log file is closed. Camera power is turned off. The lights are
 * set to show the device is not running.
 *
 * There are several delays built in to these steps, so this function
 * does not return quickly.
 *
 * @return
 *   Returns true on success, false on failure. On failure, error
 *   messages have already been printed.
 *
 * @see getCameraStatus()
 * @see getHardwareStatus()
 * @see getSoftwareStatus()
 * @see snapAndLog()
 * @see startRunning()
 */
bool stopRunning( )
{
    if ( getSoftwareStatus( ) != SOFTWARE_RUNNING )
        return false;

    Serial.printf( "Stopping...\r\n" );
    const uint32_t nEntries = FileSystem::getNumberOfDataLogEntries( );
    const char*const name = FileSystem::getDataLogFilename( );

    // Close the log file.
    FileSystem::closeDataLog( );

    // Turn off the camera and laser, if they are on.
    Camera::setPower( false );
    Laser::setPower( false );

    setCameraStatus( CAMERA_OFF );
    setSoftwareStatus( SOFTWARE_READY );

    char buf[1025];
    Serial.printf( "Ready. %ld logged entries in %s\r\n\r\n", nEntries, name );
    sprintf( buf, "Stop running. %ld logged entries in %s", nEntries, name );
    FileSystem::writeStatus( buf );
    Serial.println( );
    return true;
}





//----------------------------------------------------------------------
// Battery checking.
//----------------------------------------------------------------------
#if defined(ENABLE_BATTERY_CHECK)
/**
 * Checks batteries for low or critically low levels.
 *
 * If a battery goes low, the hardware status is changed to a warning
 * and a message is logged and printed. This does not stop the device
 * from continuing to run.
 *
 * If a battery goes critically low, the hardware and software status
 * are changed to an error state and a message is logged and printed.
 * If the device is snapping photos, that is stopped.
 *
 * The low and critcally low states latch so that as soon as a battery
 * passes the threshold, the battery's recorded state is set and it
 * won't go back until the device is rebooted (and presumably a new
 * battery is installed first).
 *
 * @see getSoftwareStatus()
 * @see setHardwareSTatus()
 * @see setSoftwareStatus()
 * @see stopRunning()
 * @see Battery::getControllerPercent()
 * @see Battery::getMainPercent()
 */
void checkBatteries( )
{
    if ( Battery::isMainPresent( ) )
    {
        const float percent = Battery::getMainPercent( );
        if ( percent < BATTERY_ERROR_PERCENT )
        {
            // Very low main battery is critical.
            if ( mainBatteryState != BATTERY_CRITICAL )
            {
                // The first time the battery goes critical, change
                // status and post a message.
#if !defined(DEBUG_BATTERY_MISSING_IS_WARNING)
                if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
                    stopRunning( );
                setHardwareStatus( HARDWARE_ERRORS );
                setSoftwareStatus( SOFTWARE_ERRORS );
#endif
                mainBatteryState = BATTERY_CRITICAL;
                FileSystem::writeStatus( "** Main battery is critically low." );
                Serial.print( "** Main battery is critically low.\r\n" );
            }
        }
        else if ( percent < BATTERY_WARN_PERCENT )
        {
            // Low main battery is a warning.
            if ( mainBatteryState == BATTERY_OK )
            {
                // The first time the battery goes low, change
                // status and post a message.
                setHardwareStatus( HARDWARE_WARNINGS );
                mainBatteryState = BATTERY_LOW;
                FileSystem::writeStatus( "** Main battery is low." );
                Serial.print( "** Main battery is low.\r\n" );
            }
        }
    }

    if ( Battery::isControllerPresent( ) )
    {
        const float percent = Battery::getControllerPercent( );
        if ( percent < BATTERY_ERROR_PERCENT )
        {
            // Very low controller battery is critical.
            if ( controllerBatteryState != BATTERY_CRITICAL )
            {
#if !defined(DEBUG_BATTERY_MISSING_IS_WARNING)
                // The first time the battery goes critical, change
                // status and post a message.
                if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
                    stopRunning( );
                setHardwareStatus( HARDWARE_ERRORS );
                setSoftwareStatus( SOFTWARE_ERRORS );
#endif
                controllerBatteryState = BATTERY_CRITICAL;
                FileSystem::writeStatus( "** Controller battery is critically low." );
                Serial.print( "** Controller battery is critically low.\r\n" );
            }
        }
        else if ( percent < BATTERY_WARN_PERCENT )
        {
            // Low controller battery is a warning.
            if ( controllerBatteryState == BATTERY_OK )
            {
                // The first time the battery goes low, change
                // status and post a message.
                setHardwareStatus( HARDWARE_WARNINGS );
                controllerBatteryState = BATTERY_LOW;
                FileSystem::writeStatus( "** Controller battery is low." );
                Serial.print( "** Controller battery is low.\r\n" );
            }
        }
    }
}
#endif





//----------------------------------------------------------------------
// Boot and loop.
//----------------------------------------------------------------------
/**
 * Boot the device.
 *
 * Initialize the SD card access, sensors, switches, etc. If everything
 * initializes properly, enter the READY state waiting for the start/stop
 * switch to be pressed. If initialization fails, enter the ERROR state.
 */
void setup( )
{
    char buf[1025];
    uint8_t nErrors     = 0;
    uint8_t nWarnings   = 0;
    bool fileSystemFail = false;

#if defined(ENABLE_USAGE_TRACKING)
    // Reset counters for usage tracking, such as uptime and the number of
    // power cycles of device components.
    resetUsage( );
#endif

    //
    // Intro.
    //
    // Initialize USB serial port. Default to 9600 baud since that's what
    // the Arduino IDE's output monitor defaults to.
    Serial.begin( 9600 );

    // DO NOT wait for the serial port to become ready. A wait causes the
    // program to hang waiting for a computer to attach to the USB port.
    // This would make the device unusable stand-alone. The downside is
    // that all of the following prints are likely to silently fail.
#if defined(DEBUG_SERIAL)
    while (!Serial) { yield( ); }
#endif

    Serial.printf( "\r\nPLT Data Logger (version %s)\r\n", VERSION );
    Serial.print( "------------------------------------------------------------\r\n" );
    Serial.print( "Initializing...\r\n" );


    //
    // Basic initialization.
    //
    // Initialize components that have no way to confirm the hardware
    // exists.
    Serial.print( "  Lights...\r\n" );
    Lights::init( );
    Serial.print( "  Camera and intensifier...\r\n" );
    Camera::init( );
    Serial.print( "  Laser...\r\n" );
    Laser::init( );
    Switches::init( );
    Commands::init( );

    setHardwareStatus( HARDWARE_BOOTING );
    setSoftwareStatus( SOFTWARE_BOOTING );
    setCameraStatus( CAMERA_OFF );


    //
    // File system initialization.
    //
    // Check that there is an SD card and it is properly formatted.
    // Failure is a critical error.
    Serial.print( "  File system...\r\n" );
    if ( !FileSystem::init( ) )
    {
        // File system fail. Possible problems:
        // - The SD card reader is wired wrong.
        // - The SD card pin is set wrong.
        // - The SD card is not inserted.
        // - The SD card's format is not FAT.
        //
        // All of these have set FileSystem error codes. Below, when
        // Commands::hwinfo() is called, the error will be reported.
        ++nErrors;

        // Since the file system init failed, there is no point in posting
        // status messages here or for further initialization problems.
        fileSystemFail = true;
    }
    else
    {
        sprintf( buf, "PLT Data Logger boot (version %s)", VERSION );
        FileSystem::writeStatus( "" );
        FileSystem::writeStatus( buf );
    }


    //
    // Battery initialization.
    //
    // Check that there are batteries present and that they can be monitored.
    // Failure may be a critical error.
    Serial.print( "  Batteries...\r\n" );
    Battery::init( );
    if ( !Battery::isControllerPresent( ) )
    {
#if defined(DEBUG_BATTERY_MISSING_IS_WARNING)
        ++nWarnings;
#else
        ++nErrors;
#endif
#if defined(ENABLE_BATTERY_CHECK)
        controllerBatteryState = BATTERY_CRITICAL;
#endif
        if ( !fileSystemFail )
        {
            sprintf( buf, "** Controller battery is missing or dead." );
            FileSystem::writeStatus( buf );
        }
    }
#if defined(ENABLE_BATTERY_CHECK)
    else
    {
        const float percent = Battery::getControllerPercent( );
        if ( percent < BATTERY_ERROR_PERCENT )
        {
            // Very low controller battery is critical.
            ++nErrors;
            controllerBatteryState = BATTERY_CRITICAL;
            FileSystem::writeStatus( "** Controller battery is critically low." );
        }
        else if ( percent < BATTERY_WARN_PERCENT )
        {
            // Low controller battery is a warning.
            ++nWarnings;
            controllerBatteryState = BATTERY_LOW;
            FileSystem::writeStatus( "** Controller battery is low." );
        }
    }
#endif

    if ( !Battery::isMainPresent( ) )
    {
#if defined(DEBUG_BATTERY_MISSING_IS_WARNING)
        ++nWarnings;
#else
        ++nErrors;
#endif
#if defined(ENABLE_BATTERY_CHECK)
        mainBatteryState = BATTERY_CRITICAL;
#endif
        if ( !fileSystemFail )
        {
            sprintf( buf, "** Main battery is missing or dead." );
            FileSystem::writeStatus( buf );
        }
    }
#if defined(ENABLE_BATTERY_CHECK)
    else
    {
        const float percent = Battery::getMainPercent( );
        if ( percent < BATTERY_ERROR_PERCENT )
        {
            // Very low main battery is critical.
            ++nErrors;
            mainBatteryState = BATTERY_CRITICAL;
            FileSystem::writeStatus( "** Main battery is critically low." );
        }
        else if ( percent < BATTERY_WARN_PERCENT )
        {
            // Low main battery is a warning.
            ++nWarnings;
            mainBatteryState = BATTERY_LOW;
            FileSystem::writeStatus( "** Main battery is low." );
        }
    }
#endif


    //
    // Clock initialization.
    Serial.print( "  Clock...\r\n" );
    if ( !Clock::init( ) )
    {
#if defined(DEBUG_CLOCK_MISSING_IS_WARNING)
        ++nWarnings;
#else
        ++nErrors;
#endif
        if ( !fileSystemFail )
        {
            sprintf( buf, "  %s not found", Clock::getClockName( ) );
            FileSystem::writeStatus( "** Clock could not be initialized" );
            FileSystem::writeStatus( buf );
        }
    }


    //
    // Sensor (inertia, pressure, temperature) initialization.
    Serial.print( "  Sensors...\r\n" );
    if ( !Sensors::init( ) )
    {
#if defined(DEBUG_SENSORS_MISSING_IS_WARNING)
        ++nWarnings;
#else
        ++nErrors;
#endif
        if ( !fileSystemFail )
        {
            FileSystem::writeStatus( "** Sensors could not be initialized" );
            if ( !Sensors::isInertiaSensorPresent( ) )
            {
                sprintf( buf, "  %s not found",
                    Sensors::getInertiaSensorName( ) );
                FileSystem::writeStatus( buf );
            }
            if ( !Sensors::isPressureSensorPresent( ) )
            {
                sprintf( buf, "  %s not found",
                    Sensors::getPressureSensorName( ) );
                FileSystem::writeStatus( buf );
            }
            if ( !Sensors::isTemperatureSensorPresent( ) )
            {
                sprintf( buf, "  %s not found",
                    Sensors::getTemperatureSensorName( ) );
                FileSystem::writeStatus( buf );
            }
        }
    }


    //
    // Load settings.
    //
    // Load the previous settings, if any. The load returns false if there
    // is no file or a storage problem occurred. Otherwise use the loaded
    // values to initialize the current settings.
    if ( !fileSystemFail )
    {
        uint32_t interval = getFrameInterval( );
        bool laser = isLaserContinuous( );
        uint8_t burst = getBurstSize( );
        if ( FileSystem::loadSettings( interval, laser, burst ) )
        {
            setFrameInterval( interval );
            setLaserContinuous( laser );
            setBurstSize( burst );
        }
        else
        {
            // No Settings file yet. Create one.
            FileSystem::saveSettings( interval, laser, burst );
        }
    }


#if defined(ENABLE_USAGE_TRACKING)
    //
    // Load usage.
    //
    // Load the usage usage so far, if any. The load returns false if there
    // is no file or a storage problem occurred.
    if ( !fileSystemFail )
    {
        if ( FileSystem::loadUsage( usage ) )
        {
            // Copy the loaded values into the Camera and Laser state
            // so that we can track them through multiple reboots.
            Camera::setNumberOfPowerOns( usage.numberOfCameraBoots );
            Camera::setUptimeSeconds( usage.cameraUptimeSeconds );
            Laser::setNumberOfPowerOns( usage.numberOfLaserBoots );
            Laser::setUptimeSeconds( usage.laserUptimeSeconds );
        }
    }
#endif


    // Decide on the hardware and software status.
    if ( nErrors > 0 )
    {
        setHardwareStatus( HARDWARE_ERRORS );
        setSoftwareStatus( SOFTWARE_ERRORS );
    }
    else if ( nWarnings > 0 )
    {
        // Report the problem and enter the READY state anyway.
        setHardwareStatus( HARDWARE_WARNINGS );
        setSoftwareStatus( SOFTWARE_READY );
    }
    else
    {
        // Report that the device is ready and enter the READY state.
        // Do not turn on the camera yet. Wait for a RUNNING state.
        setHardwareStatus( HARDWARE_READY );
        setSoftwareStatus( SOFTWARE_READY );
    }


    //
    // Show hardware info and issue messages.
    //
    Serial.println( );
    Commands::hwinfo( );
    Serial.println( );

    if ( nErrors > 0 )
    {
        Serial.print( "** Cannot run due to critical hardware problems.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
        if ( !fileSystemFail )
            FileSystem::writeStatus( "** Cannot run due to critical hardware problems." );
    }
    else if ( nWarnings > 0 )
    {
        Serial.print( "Ready for limited use despite hardware problems.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
        FileSystem::writeStatus( "Ready for limited use despite hardware problems." );
    }
    else
    {
        Serial.print( "Ready.\r\n" );
        FileSystem::writeStatus( "Ready." );
    }

    Serial.print( "Type 'help' for a list of commands.\r\n" );
    Commands::printPrompt( );


#if defined(ENABLE_USAGE_TRACKING)
    //
    // Update usage tracking.
    //
    ++usage.numberOfBoots;
    const uint32_t ut = millis( ) / 1000;
    usage.controllerUptimeSeconds += (ut - usage.recentUpdateTime);
    usage.recentUpdateTime = ut;

    if ( !fileSystemFail )
    {
        usage.numberOfCameraBoots = Camera::getNumberOfPowerOns( );
        usage.cameraUptimeSeconds = Camera::getUptimeSeconds( );
        usage.numberOfLaserBoots  = Laser::getNumberOfPowerOns( );
        usage.laserUptimeSeconds  = Laser::getUptimeSeconds( );
        FileSystem::saveUsage( usage );
    }
#endif
}





/**
 * Run loop.
 */
void loop( )
{
    uint32_t currentTime = millis( );

    // For any run state, check for and run serial port commands. This
    // allows status checks and file downloads even when there are
    // hardware errors.
    //Commands::processCommands( );

#if defined(ENABLE_BATTERY_CHECK)
    // Check batteries periodically. If a battery goes low or critically
    // low, the hardware and software status may change and snap and log
    // may stop.
    if ( (currentTime - previousBatteryCheckTime) >= BATTERY_CHECK_INTERVAL )
    {
        previousBatteryCheckTime = currentTime;
        checkBatteries( );
    }
#endif

    // Updates switch state.
    Switches::update( );

    // When in an error state, parts of the device are not working and there
    // is no further work that can be done. Ignore any switches or sensors.
    switch ( getSoftwareStatus( ) )
    {
        default:
        case SOFTWARE_OFF:
        case SOFTWARE_BOOTING:
            // Not possible.
            return;

        case SOFTWARE_ERRORS:
            // When there are software errors, we can't keep running.
            return;

        case SOFTWARE_READY:
        case SOFTWARE_RUNNING:
            break;
    }

    // If the start/stop switch is pressed, toggle between RUNNING and
    // READY states.
    if ( Switches::isStartStopPressed( ) )
    {
        if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
            stopRunning( );
        else
        {
            // This automatically takes the first photo.
            startRunning( );
        }
        return;
    }

    // While running, check if enough time has elapsed since the last
    // snapshot and log entry.  If so, snap a picture and log sensors.
    if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
    {
        currentTime = millis( );
        if ( (currentTime - previousLogTime) >= frameInterval )
        {
            previousLogTime = currentTime;
            snapAndLog( getBurstSize( ) );
        }
    }
}





/**
 * Serial event handler.
 *
 * This function is automatically called by the Arduino run-time after each
 * call to loop(), if there is data available on the serial input.
 *
 * Note that on some Arduino's, the function should be serialEvent().
 */
void serialEventRun( )
{
    Commands::handleSerialInput( );
}
