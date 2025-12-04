#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <Arduino.h>

#include "pltlogger.h"
#include "pins.h"


/**
 * Manages the camera and intensifier.
 *
 * The device uses:
 *
 * - A Sony DSC-RX0M2 digital camera. A microcontroller pin is routed to
 *   the camera's shutter. Another pair of pins are routed to a relay that
 *   powers on/off the camera.
 *
 * - An intensifier in front of the camera lens to brigten dim content.
 *   A pair of microcontroller pins are routed to a relay that powers
 *   on/off the intensifier.
 */
class Camera
{
private:
    Camera( ) = delete;
    Camera( const Camera& ) = delete;
    Camera& operator=( const Camera& ) = delete;


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
    static const uint8_t CAMERA_SHUTTER_DELAY  = 50;    // ms.
    static const uint8_t CAMERA_RELAY_DELAY    = 10;    // ms.
    static const uint16_t CAMERA_POWERUP_DELAY = 15000; // ms.


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    // Whether the camera power is on or off.
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
     * Initializes the camera and intensifier.
     */
    static inline void init( )
    {
        // The camera shutter, power, and intensifier power are all
        // output pins.
        pinMode( CAMERA_SHUTTER_PIN, OUTPUT );
        pinMode( CAMERA_POWER_SET_PIN, OUTPUT );
        pinMode( INTENSIFIER_POWER_SET_PIN, OUTPUT );
        pinMode( INTENSIFIER_POWER_UNSET_PIN, OUTPUT );

        // Make sure the intensifier is off.
        digitalWrite( INTENSIFIER_POWER_UNSET_PIN, HIGH );
        delay( CAMERA_RELAY_DELAY );
        digitalWrite( INTENSIFIER_POWER_UNSET_PIN, LOW );

        // There is no way to insure that the camera is off. Assume it
        // is and keep track of it from now on.
        powerStatus = false;
#if defined(DEBUG_VERBOSE_CAMERA)
        Serial.print( "Debug: Camera and intensifier initialized.\r\n" );
#endif

#if defined(ENABLE_USAGE_TRACKING)
        resetUsage( );
#endif
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
     * Returns true if the camera and intensifier are powered on.
     *
     * @return
     *   Returns true if on.
     *
     * @see setPower()
     */
    static inline bool isPowerOn( )
    {
        // There is no way to query the camera and intensifier power
        // state, so we keep track of it ourselves.
        return powerStatus;
    }

    /**
     * Turns the power on/off on the camera and intensifier.
     *
     * The camera power is controlled by a toggle pin. The software has to
     * keep track of whether the pin has been toggled an even number of times
     * (the camera is therefore off) or an odd number (the camera is on).
     *
     * The intensifier power is controlled by separate on and off pins.
     * The software doesn't need to keep track of the presumed intensifier
     * state. It just sets it on or off as needed.
     *
     * It is possible for the software to get out of sync with the camera's
     * power if the camera has been turned on or off physically or via
     * different software without a new boot. In this case, the user can
     * issue a 'force off' by setting the 'force' parameter to TRUE while
     * setting the 'onOff' parameter to 'FALSE'.
     *
     *   onOff   force   Result
     *   -----   -----   ------
     *   TRUE    FALSE   If not already on, turn on camera & intensifier.
     *   FALSE   FALSE   If not already off, turn off camera & intensifier.
     *   TRUE    TRUE    Toggle camera and turn on intensifier.
     *   FALSE   TRUE    Toggle camera and turn off intensifier.
     *
     * @param[in] onOff
     *   True to turn the camera and intensifier on, and false to
     *   turn them off.
     * @param[in] force
     *   True to force on/off even when the camera status thinks it
     *   is already in the right on/off state.
     *
     * @see isPowerOn()
     * @see snap()
     */
    static inline void setPower( const bool onOff, const bool force = false )
    {
        // Abort if the current power state matches the desired state.
        // But ignore the state if we're forcing the action.
        if ( force == false && powerStatus == onOff )
            return;

        // To power the camera and intensifier on or off we set the associated
        // relay HIGH then LOW a moment later, causing the relay to latch
        // or unlatch. Because there is no way for us to know the current
        // state of the relay, we have to keep track of it ourselves.

        // Turn on/off camera. Upon completion, we *presume* the camera is
        // in the intended on/off state. There is no way to be sure.
#if defined(DEBUG_VERBOSE_CAMERA)
        Serial.printf( "Debug: Camera power %s.\r\n",
            onOff ? "ON" : "OFF" );
#endif

        digitalWrite( CAMERA_POWER_SET_PIN, HIGH );
        delay( CAMERA_RELAY_DELAY );
        digitalWrite( CAMERA_POWER_SET_PIN, LOW );

        // Turn on/off intensifier. Because the intensifier has separate
        // on and off pins, this always leaves the intensifier in the
        // intended state.
#if defined(DEBUG_VERBOSE_CAMERA)
        Serial.printf( "Debug: Camera intensifier power %s.\r\n",
            onOff ? "ON" : "OFF" );
#endif

        if ( onOff )
        {
            digitalWrite( INTENSIFIER_POWER_SET_PIN, HIGH );
            delay( CAMERA_RELAY_DELAY );
            digitalWrite( INTENSIFIER_POWER_SET_PIN, LOW );
        }
        else
        {
            digitalWrite( INTENSIFIER_POWER_UNSET_PIN, HIGH );
            delay( CAMERA_RELAY_DELAY );
            digitalWrite( INTENSIFIER_POWER_UNSET_PIN, LOW );
        }

        // On power up, wait for the camera and intensifier to finish
        // powering up before continuing.
        if ( onOff )
        {
#if defined(DEBUG_VERBOSE_CAMERA)
            Serial.printf( "Debug: Camera power ON delay for %d ms.\r\n",
                CAMERA_POWERUP_DELAY );
#endif
            delay( CAMERA_POWERUP_DELAY );
        }

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
// Use methods.
//----------------------------------------------------------------------
public:
    /**
     * Snaps a picture with the camera.
     *
     * If the camera power is off, an error message is output and no
     * action is taken.
     *
     * @param[in] nImages
     *   The number of images to snap in a burst.
     *
     * @see isPowerOn()
     * @see setPower()
     */
    static inline void snap( const uint8_t nImages )
    {
        if ( !powerStatus )
            return;

#if defined(DEBUG_VERBOSE_CAMERA)
        Serial.printf( "Debug: Camera shutter of %d images.\r\n", nImages );
#endif

        for ( uint8_t i = 0; i < nImages; ++i )
        {
            digitalWrite( CAMERA_SHUTTER_PIN, HIGH );
            delay( CAMERA_RELAY_DELAY );
            digitalWrite( CAMERA_SHUTTER_PIN, LOW );
            delay( CAMERA_SHUTTER_DELAY );
        }
    }
};
