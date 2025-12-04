#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <Arduino.h>

#include "pins.h"


/**
 * Manages device switches.
 *
 * The device has three switches:
 *
 * - A waterproof power switch accessible outside of the pressure case.
 *   Turning on the power boots the microcontroller, which then turns on
 *   and initializes the rest of the device.
 *
 * - A start/stop switch accessible via a magnetic trigger from outside of
 *   the pressure case. The switch starts and stops photography and sensor
 *   logging.
 *
 * - A reset switch on the processor and add-on boards. The reset switch is
 *   used to reset the processor after a hang.
 *
 * This class only handles the start/stop switch.
 */
class Switches
{
private:
    Switches( ) = delete;
    Switches( const Switches& ) = delete;
    Switches& operator=( const Switches& ) = delete;

//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
    static const uint32_t SWITCH_DEBOUNCE_PERIOD = 50; // ms
    static const uint16_t SWITCH_DOWN = 0;
    static const uint16_t SWITCH_UP   = 1;


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    static uint32_t lastDebounceMillis;
    static uint16_t count;
    static uint16_t previousSteadyState;
    static uint16_t lastSteadyState;
    static uint16_t lastFlickerableState;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the switches.
     */
    static inline void init( )
    {
#if defined(DEBUG_VERBOSE_SWITCHES)
        Serial.print( "Debug: Switches initialized using custom code.\r\n" );
#endif
        // Initialize the switch pin to be for input.
        pinMode( STARTSTOP_SWITCH_PIN, INPUT_PULLUP );

        // Get and save the initial state.
        previousSteadyState = lastSteadyState = lastFlickerableState =
            digitalRead( STARTSTOP_SWITCH_PIN );
        lastDebounceMillis = 0;
        count = 0;

        // There is no way to verify that the switch is present and working.
    }


//----------------------------------------------------------------------
// Methods.
//----------------------------------------------------------------------
public:
    /**
     * Updates switch state.
     */
    static inline void update( )
    {
        // Check the switch's current state. It will be either:
        // - 0 for switch down.
        // - 1 for switch up.
        const uint16_t currentState = digitalRead( STARTSTOP_SWITCH_PIN );

        // If the state has changed, update the debounce timer and
        // save the new state.
        if ( currentState != lastFlickerableState )
        {
            lastDebounceMillis = millis( );
            lastFlickerableState = currentState;
        }

        // If we've exceed the debounce period, lock in the current state.
        if ( (millis( ) - lastDebounceMillis) >= SWITCH_DEBOUNCE_PERIOD )
        {
            previousSteadyState = lastSteadyState;
            lastSteadyState = currentState;
        }

        // If the state has changed, increment a counter.
        if ( previousSteadyState != lastSteadyState &&
             lastSteadyState == SWITCH_UP )
        {
            ++count;
        }
    }

    /**
     * Returns true if the start/stop switch is currently pressed.
     *
     * @return
     *   True if pressed.
     */
    static inline bool isStartStopPressed( )
    {
        if ( count > 0 )
        {
            count = 0;
#if defined(DEBUG_VERBOSE_SWITCHES)
            Serial.print( "Debug: Switches button pressed.\r\n" );
#endif
            return true;
        }
        return false;
    }
};
