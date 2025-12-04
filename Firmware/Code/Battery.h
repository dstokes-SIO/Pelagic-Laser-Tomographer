#pragma once
#include <Arduino.h>

//----------------------------------------------------------------------
// Features.
//----------------------------------------------------------------------
// An LC709203F battery monitor should be installed to monitor the main
// (camera, intensifier, laser) and controller batteries. This allows
// levels (percent) to be checked with some reliability (see
// BATTERY_USE_LC709203F). Without them, only an approximate voltage
// can be checked (see BATTERY_USE_RAW_VOLTAGE).
#define BATTERY_USE_RAW_VOLTAGE 0
#define BATTERY_USE_LC709203F 1
//
// Optional but recommended. Define to enable main battery monitoring.
// When enabled, define with a value of BATTERY_USE_RAW_VOLTAGE or
// BATTERY_USE_LC709203F.
#define BATTERY_ENABLE_MAIN_MONITORING BATTERY_USE_LC709203F

// Optional but recommended. Define to enable controller battery monitoring.
// When enabled, define with a value of BATTERY_USE_RAW_VOLTAGE or
// BATTERY_USE_LC709203F.
#define BATTERY_ENABLE_CONTROLLER_MONITORING BATTERY_USE_LC709203F


#if defined(BATTERY_ENABLE_MAIN_MONITORING) && \
    defined(BATTERY_ENABLE_CONTROLLER_MONITORING)
// When both batteries are monitored using LC709203F monitors, the two
// monitors share the I2C bus and a MUX is required.
#include <Wire.h>

// The main battery's mux address.
#define MUX_MAIN_BATTERY       4

// The main battery's mux address.
#define MUX_CONTROLLER_BATTERY 2
#endif

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F || \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
#include <Adafruit_LC709203F.h>
#endif

#include "pltlogger.h"
#include "pins.h"


/**
 * Manages the device's batteries.
 *
 * The device has two batteries:
 *
 * - A small battery for the microcontroller and its lights, sensors, and
 *   relays for controlling other devices.
 *
 * - A large battery for the camera, intensifier, and laser.
 *
 * Both batteries are monitored by a dedicated LC709203F battery level
 * monitor. The monitors are multiplexed using a TCA9548A mux.
 *
 * @see https://github.com/adafruit/Adafruit_LC709203F
 * @see https://learn.adafruit.com/adafruit-tca9548a-1-to-8-i2c-multiplexer-breakout/overview
 */
class Battery
{
//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_RAW_VOLTAGE
    static const uint16_t MISSING_MAIN_LEVEL = 650;
#endif

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_RAW_VOLTAGE
    static const uint16_t MISSING_CONTROLLER_LEVEL = 650;
#endif

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F && \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
    static const uint32_t TCA9548A_MUX_ADDRESS = 0x70;
#endif


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
    static Adafruit_LC709203F mainBattery;
#endif

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
    static Adafruit_LC709203F controllerBattery;
#endif

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F && \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
    static uint8_t currentMuxDevice;
#endif

    static bool mainInitialized;
    static bool controllerInitialized;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the battery.
     *
     * @return
     *   Returns true on success and false on failure.
     *
     * @see isControllerPresent()
     * @see isMainPresent();
     */
    static inline bool init( )
    {
        initMux( );

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        // Use an LC709203F battery monitor.
        setMux( MUX_CONTROLLER_BATTERY);
        if ( controllerBattery.begin( ) )
        {
#if defined(DEBUG_VERBOSE_BATTERY)
            Serial.print( "Debug: Controller battery monitor initialized.\r\n" );
#endif
            // The controller battery pack is 2500 MAH. 2000 is the closest.
            controllerBattery.setPackSize( LC709203F_APA_2000MAH );
            // No alarm.
            controllerBattery.setAlarmVoltage( 0.0 );
            // No thermistor in the battery pack.
            controllerBattery.setTemperatureMode( LC709203F_TEMPERATURE_I2C );
            controllerInitialized = true;
        }
#if defined(DEBUG_VERBOSE_BATTERY)
        else
        {
            Serial.print( "Debug: Controller battery monitor initialization FAIL.\r\n" );
        }
#endif

#elif BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_RAW_VOLTAGE
        // Use raw voltage monitoring.
        const uint16_t craw = analogRead( CONTROLLER_BATTERY_VOLTAGE_PIN );
        controllerInitialized = (craw < MISSING_CONTROLLER_LEVEL);
#if defined(DEBUG_VERBOSE_BATTERY)
        if ( controllerInitialized )
        {
            Serial.print( "Debug: Controller battery raw voltage monitor initialized.\r\n" );
        }
        else
        {
            Serial.print( "Debug: Controller battery raw voltage monitor initialization FAIL.\r\n" );
        }
#endif

#else
        // Don't monitor.
        controllerInitialized = false;
#endif

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
        setMux( MUX_MAIN_BATTERY);
        if ( mainBattery.begin( ) )
        {
#if defined(DEBUG_VERBOSE_BATTERY)
            Serial.print( "Debug: Main battery monitor initialized.\r\n" );
#endif
            // The main battery pack is > 3000 MAH, so use 3000.
            mainBattery.setPackSize( LC709203F_APA_3000MAH );
            // No alarm.
            mainBattery.setAlarmVoltage( 0.0 );
            // No thermistor in the battery pack.
            mainBattery.setTemperatureMode( LC709203F_TEMPERATURE_I2C );
            mainInitialized = true;
        }
#if defined(DEBUG_VERBOSE_BATTERY)
        else
        {
            Serial.print( "Debug: Main battery monitor initialization FAIL.\r\n" );
        }
#endif

#elif BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_RAW_VOLTAGE
        // Use raw voltage monitoring.
        const uint16_t mraw = analogRead( MAIN_BATTERY_VOLTAGE_PIN );
        mainInitialized = (mraw < MISSING_MAIN_LEVEL);
#if defined(DEBUG_VERBOSE_BATTERY)
        if ( mainInitialized )
        {
            Serial.print( "Debug: Main battery raw voltage monitor initialized.\r\n" );
        }
        else
        {
            Serial.print( "Debug: Main battery raw voltage monitor initialization FAIL.\r\n" );
        }
#endif

#else
        // Don't monitor.
        mainInitialized = false;
#endif

        return mainInitialized && controllerInitialized;
    }

private:
    /**
     * Initializes the mux when two LC709203F's are used.
     *
     * @see setMux()
     */
    static inline void initMux( )
    {
#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F && \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        // Initialize the I2C bus connection so we can talk to the mux.
        Wire.begin( );
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.print( "Debug: Battery I2C mux initialized.\r\n" );
#endif
#endif
    }

    /**
     * Sets the mux to the main (0) or controller (1) battery monitor.
     *
     * @param[in] device
     *   The battery monitor to set the mux to.
     *
     * @see initMux()
     * @see getControllerPercent()
     * @see getControllerVoltage()
     * @see getMainPercent()
     * @see getMainVoltage()
     */
    static inline void setMux( const uint8_t device )
    {
#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F && \
    BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        // If both battery monitors are enabled, set the mux.
        if ( device > 7 )
            return;
        if ( device == currentMuxDevice )
            return;
        Wire.beginTransmission( TCA9548A_MUX_ADDRESS );
        Wire.write( 1 << device );
        Wire.endTransmission( );
        currentMuxDevice = device;
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Battery I2C mux set to device %d.\r\n", device );
#endif
#endif
    }

public:
    /**
     * Returns the name of the micronctroller battery monitor.
     *
     * @return
     *   Returns the name.
     *
     * @see getControllerMonitorName()
     * @see getControllerPercent()
     * @see getControllerVoltage()
     * @see isControllerPresent()
     * @see init()
     */
    static inline const char* getControllerMonitorName( )
    {
#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        return "LC709203F controller battery monitor";
#elif BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_RAW_VOLTAGE
        return "Controller battery raw voltage monitor";
#else
        return "Unmonitored controller battery";
#endif
    }

    /**
     * Returns true if the microcontroller battery is present.
     *
     * @return
     *   Returns true if the battery is present.
     *
     * @see getControllerMonitorName()
     * @see getControllerPercent()
     * @see getControllerVoltage()
     * @see init()
     * @see isMainPresent()
     */
    static inline bool isControllerPresent( )
    {
        return controllerInitialized;
    }

    /**
     * Returns the name of the main battery monitor.
     *
     * @return
     *   Returns the name.
     *
     * @see getMainMonitorName()
     * @see getMainPercent()
     * @see getMainVoltage()
     * @see isMainPresent()
     * @see init()
     */
    static inline const char* getMainMonitorName( )
    {
#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
        return "LC709203F main battery monitor";
#elif BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_RAW_VOLTAGE
        return "Main battery raw voltage monitor";
#else
        return "Unmonitored main battery";
#endif
    }

    /**
     * Returns true if the main battery is present.
     *
     * @return
     *   Returns true if the battery is present.
     *
     * @see getMainMonitorName()
     * @see getMainPercent()
     * @see getMainVoltage()
     * @see init()
     * @see isControllerPresent()
     */
    static inline bool isMainPresent( )
    {
        return mainInitialized;
    }



//----------------------------------------------------------------------
// Methods.
//----------------------------------------------------------------------
public:
    /**
     * Returns the current microcontroller battery charge percent.
     *
     * @return
     *   Returns the percent.
     *
     * @see getControllerVoltage()
     * @see isControllerPresent()
     * @see setMux()
     */
    static inline float getControllerPercent( )
    {
        if ( !controllerInitialized )
            return 0.0;

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        // Read the controller battery's monitor.
        setMux( MUX_CONTROLLER_BATTERY );
        const float percent = controllerBattery.cellPercent( );
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Controller battery level = %f%%.\r\n", percent );
#endif
        return percent;
#else
        // Without knowing the battery's discharge profile, there is no
        // way to compute the percentage.
        return 0.0;
#endif
    }

    /**
     * Returns the current microcontroller battery voltage.
     *
     * @return
     *   Returns the voltage.
     *
     * @see getControllerPercent()
     * @see isControllerPresent()
     * @see setMux()
     */
    static inline float getControllerVoltage( )
    {
        if ( !controllerInitialized )
            return 0.0;

#if BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_LC709203F
        // Read the controller battery's monitor.
        setMux( MUX_CONTROLLER_BATTERY );
        const float v = controllerBattery.cellVoltage( );
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Controller battery voltage = %f volts.\r\n", v );
#endif
        return v;
#elif BATTERY_ENABLE_CONTROLLER_MONITORING == BATTERY_USE_RAW_VOLTAGE
        // Read the raw voltage. If it is invalid, return zero.
        const uint16_t raw = analogRead( CONTROLLER_BATTERY_VOLTAGE_PIN );
        if ( raw >= MISSING_CONTROLLER_LEVEL )
            return 0.0;

        // Convert a 0..1023 value to 0..1.0, multiply by 2 to compensate
        // for a divide by 2 from the voltage divider. Then multiply by
        // the 3.3V reference voltage. This gives a current voltage.
        const float v = ((float) raw) / 1023.0 * 2.0 * 3.3;
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Controller battery voltage = %f volts.\r\n", v );
#endif
        return v;
#else
        return 0.0;
#endif
    }

    /**
     * Returns the current main battery charge percent.
     *
     * @return
     *   Returns the percent.
     *
     * @see getMainVoltage()
     * @see isMainPresent()
     * @see setMux()
     */
    static inline float getMainPercent( )
    {
        if ( !mainInitialized )
            return 0.0;

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
        // Read the main battery's monitor.
        setMux( MUX_MAIN_BATTERY );
        const float percent = mainBattery.cellPercent( );
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Main battery level = %f%%.\r\n", percent );
#endif
        return percent;
#else
        // Without knowing the battery's discharge profile, there is no
        // way to compute the percentage.
        return 0.0;
#endif
    }

    /**
     * Returns the current main battery voltage.
     *
     * @return
     *   Returns the voltage.
     *
     * @see getMainPercent()
     * @see isMainPresent()
     * @see setMux()
     */
    static inline float getMainVoltage( )
    {
        if ( !mainInitialized )
            return 0.0;

#if BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_LC709203F
        // Read the main battery's monitor.
        setMux( MUX_MAIN_BATTERY );
        const float v = mainBattery.cellVoltage( );
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Main battery voltage = %f volts.\r\n", v );
#endif
        return v;
#elif BATTERY_ENABLE_MAIN_MONITORING == BATTERY_USE_RAW_VOLTAGE
        // Read the raw voltage. If it is invalid, return zero.
        const uint16_t raw = analogRead( MAIN_BATTERY_VOLTAGE_PIN );
        if ( raw >= MISSING_MAIN_LEVEL )
            return 0.0;

        // Convert a 0..1023 value to 0..1.0, multiply by 2 to compensate
        // for a divide by 2 from the voltage divider. Then multiply by
        // the 3.3V reference voltage. This gives a current voltage.
        cosnt float v = ((float) raw) / 1023.0 * 2.0 * 3.3;
#if defined(DEBUG_VERBOSE_BATTERY)
        Serial.printf( "Debug: Main battery voltage = %f volts.\r\n", v );
#endif
        return v;
#else
        return 0.0;
#endif
    }
};
