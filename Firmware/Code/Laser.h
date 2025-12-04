#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <Arduino.h>

#include "pltlogger.h"
#include "pins.h"


/**
 * Manages the laser.
 *
 * The device uses a laser and optics to project a light sheet outwards
 * and within view of the device's camera. To conserve power, the laser
 * should be turned on only while capturing a photo.
 */
class Laser
{
private:
    Laser( ) = delete;
    Laser( const Laser& ) = delete;
    Laser& operator=( const Laser& ) = delete;

//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
    static const uint8_t LASER_WARMUP_DELAY = 50; // ms


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    // Whether the laser power is on or off.
    static bool powerStatus;

#if defined(ENABLE_USAGE_TRACKING)
    // Usage counters and uptime.
    static uint32_t numberOfPowerOns;
    static uint32_t uptimeSeconds;
    static uint32_t recentPowerOnTime;
#endif


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the laser.
     */
    static inline void init( )
    {
        pinMode( LASER_PIN, OUTPUT );

        // The laser power is initially off. Since there is no way to
        // detect if the power is on, we keep track of it instead.
        powerStatus = false;
#if defined(DEBUG_VERBOSE_LASER)
        Serial.print( "Debug: Laser initialized.\r\n" );
#endif

#if defined(ENABLE_USAGE_TRACKING)
        resetUsage( );
#endif

        // Cycle the laser to show it is working. End with it off.
        testCycle( );

        // There is no way to verify that the laser is present.
    }


#if defined(ENABLE_USAGE_TRACKING)
//----------------------------------------------------------------------
// Usage.
//----------------------------------------------------------------------
private:
    /**
     * Resets usage tracking.
     *
     * The number of power ons and uptime are reset to zeroes.
     */
    static inline void resetUsage( )
    {
        numberOfPowerOns = 0;
        uptimeSeconds = 0;
        recentPowerOnTime = 0;
    }

public:
    /**
     * Returns the number of power-ons tracked.
     *
     * @return
     *   The number of times powered on.
     *
     * @see setNumberOfPowerOns()
     */
    static inline uint32_t getNumberOfPowerOns( )
    {
        return numberOfPowerOns;
    }

    /**
     * Returns the number of seconds of power on time.
     *
     * @return
     *   The number of seconds powered on.
     *
     * @see setUptimeSeconds()
     */
    static inline uint32_t getUptimeSeconds( )
    {
        if ( recentPowerOnTime == 0 )
            return uptimeSeconds;
        return uptimeSeconds + millis( ) / 1000 - recentPowerOnTime;
    }

    /**
     * Sets the number of power-ons tracked.
     *
     * @param[in] n
     *   The number of times powered on.
     *
     * @see getNumberOfPowerOns()
     */
    static inline void setNumberOfPowerOns( const uint32_t n )
    {
        numberOfPowerOns = n;
    }

    /**
     * Sets the number of seconds of power on time.
     *
     * @param[in] secs
     *   The number of seconds powered on.
     *
     * @see getUptimeSeconds()
     */
    static inline void setUptimeSeconds( const uint32_t secs )
    {
        uptimeSeconds = secs;
    }
#endif


//----------------------------------------------------------------------
// Power.
//----------------------------------------------------------------------
public:
    /**
     * Returns true if the laser is powered on.
     *
     * @return
     *   Returns true if on.
     *
     * @see setPower()
     */
    static inline bool isPowerOn( )
    {
        // There is no way to query the laser power state,
        // so we keep track of it ourselves.
        return powerStatus;
    }

    /**
     * Turn the laser on or off.
     *
     * When the laser is turned on, there is a short delay before the
     * method returns so that the laser has time to warm up and stabilize.
     *
     * @param[in] onOff
     *   True to turn the laser on, and false to turn it off.
     *
     * @see isPowerOn()
     */
    static inline void setPower( const bool onOff )
    {
#if defined(DEBUG_VERBOSE_LASER)
        Serial.printf( "Debug: laser power %s.\r\n",
            onOff ? "ON" : "OFF" );
#endif

        if ( powerStatus == onOff )
            return;     // Already in the desired power state.

        digitalWrite( LASER_PIN, onOff ? HIGH : LOW );

        // On power on, wait for the laser to warm up and stabilize.
        if ( onOff )
            delay( LASER_WARMUP_DELAY );
        powerStatus = onOff;

#if defined(ENABLE_USAGE_TRACKING)
        if ( powerStatus )
        {
            // Count power ons.
            ++numberOfPowerOns;
            recentPowerOnTime = millis( ) / 1000;
        }
        else if ( recentPowerOnTime > 0 )
        {
            // Record duration of power on.
            uptimeSeconds += millis( ) / 1000 - recentPowerOnTime;
            recentPowerOnTime = 0;
        }
#endif
    }


//----------------------------------------------------------------------
// Test methods.
//----------------------------------------------------------------------
public:
    /**
     * Cycles the laser on and off a few times to show it is working.
     *
     * Cycling ends with the laser off.
     *
     * @see isPowerOn()
     * @see setPower()
     */
    static inline void testCycle( )
    {
        for ( uint8_t i = 3; i > 0; --i )
        {
            setPower( true );
            delay( LASER_WARMUP_DELAY );
            setPower( false );
            if ( i != 1 )
                delay( LASER_WARMUP_DELAY );
        }
    }
};
