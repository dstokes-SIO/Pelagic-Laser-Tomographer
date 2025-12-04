#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "pltlogger.h"
#include "pins.h"


/**
 * Manages device lights (LEDs).
 *
 * The device includes the following lights:
 *
 * - Red LED on the Adafruit Feather M0 board.
 *
 * - Green LED on the Adafruit Feather M0 board.
 *
 * - Three multi-color NeoPix LEDs mounted behind a window on the pressure case.
 *
 * The board red and green LEDs are strictly used for debugging since they
 * are not visible once the device is within its pressure case.
 *
 * NeoPix LEDs have the following meanings:
 * - #1 = Hardware status:
 *   - Off = turned off.
 *   - Blue = booting.
 *   - Red = errors.
 *   - Yellow = warnings.
 *   - Green = OK.
 *
 * - #2 = Software status:
 *   - Off = turned off.
 *   - Blue = booting.
 *   - Red = problems.
 *   - Green = ready and running.
 *
 * - #3 = Camera status:
 *   - Off = turned off.
 *   - Blue = turning on.
 *   - Green = ready to shoot.
 *   - White = shooting.
 *
 * @see https://github.com/adafruit/Adafruit_NeoPixel
 */
class Lights
{
private:
    Lights( ) = delete;
    Lights( const Lights& ) = delete;
    Lights& operator=( const Lights& ) = delete;


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
    // The number of NeoPix LEDs.
    static const uint8_t NUMBER_OF_NEOPIXELS = 3;

    // Maximum NeoPix level to use. Values range from 0 to 255, but
    // the observed brightness change is non-linear. A value of '10'
    // is substantially brighter than a '1', but only a little dimmer
    // than a '32'. The level chosen here is suitable for the LEDs to
    // be visible in daylight, while not being so bright they light
    // up the area near the device.
    static const uint8_t MAX_BRIGHTNESS = 10;

    static const uint8_t HARDWARE_PIXEL = 0;
    static const uint8_t SOFTWARE_PIXEL = 1;
    static const uint8_t CAMERA_PIXEL   = 2;

    // Maximum message buffer size.
    static const uint8_t BUFFER_SIZE = 80;


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    // NeoPix colored pixel control.
    static Adafruit_NeoPixel pixels;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the LEDs.
     *
     * @see testCycle()
     */
    static inline void init( )
    {
        // Initialize NeoPixels. No error flag is returned, so there is
        // no way to know if these pixels are connected.
        pixels.begin( );

        // Initialize board LED pins to be for output.
        pinMode( BOARD_RED_LED_PIN, OUTPUT );
        pinMode( BOARD_GREEN_LED_PIN, OUTPUT );

        // Cycle all of the lights to show they are working. End with
        // all lights off.
        testCycle( );

        // There is no way to verify that the lights are present and working.
#if defined(DEBUG_VERBOSE_LIGHTS)
        Serial.printf( "  Debug: Lights initialized.\r\n" );
#endif
    }

    /**
     * Cycles the lights to show they are working.
     *
     * Cycling ends with all lights off.
     *
     * @see init()
     * @see setBoardGreen()
     * @see setBoardRed()
     * @see setNeopix()
     */
    static void testCycle( )
    {
        const uint32_t PAUSE = 50; // ms

        setBoardRed( true );
        setBoardGreen( true );
        uint16_t r = MAX_BRIGHTNESS, g = 0, b = 0;
        for ( uint8_t j = 0; j < NUMBER_OF_NEOPIXELS; ++j )
        {
            setNeopix( j, r, g, b );
            delay( PAUSE );
        }
        setBoardRed( false );
        setBoardGreen( false );

        r = 0;
        g = MAX_BRIGHTNESS;
        b = 0;
        for ( uint8_t j = 0; j < NUMBER_OF_NEOPIXELS; ++j )
        {
            setNeopix( j, r, g, b );
            delay( PAUSE );
        }

        setBoardRed( true );
        setBoardGreen( true );
        r = 0;
        g = 0;
        b = MAX_BRIGHTNESS;
        for ( uint8_t j = 0; j < NUMBER_OF_NEOPIXELS; ++j )
        {
            setNeopix( j, r, g, b );
            delay( PAUSE );
        }
        setBoardRed( false );
        setBoardGreen( false );

        r = MAX_BRIGHTNESS;
        g = MAX_BRIGHTNESS;
        b = MAX_BRIGHTNESS;
        for ( uint8_t j = 0; j < NUMBER_OF_NEOPIXELS; ++j )
        {
            setNeopix( j, r, g, b );
            delay( PAUSE );
        }

        setBoardRed( true );
        setBoardGreen( true );
        r = 0;
        g = 0;
        b = 0;
        for ( uint8_t j = 0; j < NUMBER_OF_NEOPIXELS; ++j )
        {
            setNeopix( j, r, g, b );
            delay( PAUSE );
        }
        setBoardRed( false );
        setBoardGreen( false );
    }


//----------------------------------------------------------------------
// Light methods.
//----------------------------------------------------------------------
public:
    /**
     * Returns a string describing the current light state.
     *
     * @return
     *   Returns a string.
     */
    static const char* getLightString( )
    {
        static char sharedBuffer[BUFFER_SIZE];
        const char* h = "";
        switch ( getHardwareStatus( ) )
        {
            default:
            case HARDWARE_OFF:      h = "---"; break;
            case HARDWARE_BOOTING:  h = "Blue"; break;
            case HARDWARE_ERRORS:   h = "Red"; break;
            case HARDWARE_WARNINGS: h = "Yellow"; break;
            case HARDWARE_READY:    h = "Green"; break;
        }

        const char* s = "";
        switch ( getSoftwareStatus( ) )
        {
            default:
            case SOFTWARE_OFF:     s = "---"; break;
            case SOFTWARE_BOOTING: s = "Blue"; break;
            case SOFTWARE_ERRORS:  s = "Red"; break;
            case SOFTWARE_READY:   s = "Green"; break;
            case SOFTWARE_RUNNING: s = "Green"; break;
        }

        const char* c = "";
        switch ( getCameraStatus( ) )
        {
            default:
            case CAMERA_OFF:      c = "---"; break;
            case CAMERA_BOOTING:  c = "Blue"; break;
            case CAMERA_READY:    c = "Green"; break;
            case CAMERA_SHOOTING: c = "White"; break;
        }

        sprintf( sharedBuffer, "H/W (%s)  S/W (%s)  Camera (%s)", h, s, c );
        return sharedBuffer;
    }

    /**
     * Resets all lights to show status.
     */
    static inline void reset( )
    {
        setBoardGreen( false );
        setBoardRed( false );
        setLightsForStatus( );
    }

    /**
     * Turns the green board LED on or off.
     *
     * @param[in] onOff
     *   True turns the LED on, while false turns it off.
     *
     * @see setBoardRed()
     * @see setNeopix()
     */
    static inline void setBoardGreen( const bool onOff )
    {
#if defined(DEBUG_VERBOSE_LIGHTS)
        Serial.printf( "  Debug: Board green LED %s\r\n",
            onOff ? "ON" : "OFF" );
#endif
        digitalWrite( BOARD_GREEN_LED_PIN, onOff ? HIGH : LOW );
    }

    /**
     * Turns the red board LED on or off.
     *
     * @param[in] onOff
     *   True turns the LED on, while false turns it off.
     *
     * @see setBoardGreen()
     * @see setNeopix()
     */
    static inline void setBoardRed( const bool onOff )
    {
#if defined(DEBUG_VERBOSE_LIGHTS)
        Serial.printf( "  Debug: Board red LED %s\r\n",
            onOff ? "ON" : "OFF" );
#endif
        digitalWrite( BOARD_RED_LED_PIN, onOff ? HIGH : LOW );
    }

    /**
     * Turns the multi-colored status LED to a specific color.
     *
     * @param[in] red
     *   Sets the red component of the LED color.
     * @param[in] green
     *   Sets the green component of the LED color.
     * @param[in] blue
     *   Sets the blue component of the LED color.
     *
     * @see setBoardGreen()
     * @see setBoardRed()
     */
    static inline void setNeopix(
        const uint8_t index,
        const uint16_t red,
        const uint16_t green,
        const uint16_t blue )
    {
#if defined(DEBUG_VERBOSE_LIGHTS)
        Serial.printf( "  Debug: Neopix LED %d color %d, %d, %d\r\n",
            index, red, green, blue );
#endif
        // Set the color and update.
        pixels.setPixelColor( index, pixels.Color( red, green, blue ) );
        pixels.show( );
    }


//----------------------------------------------------------------------
// Purpose-specific light methods.
//----------------------------------------------------------------------
public:
    /**
     * Sets lights to reflect the current device status.
     */
    static inline void setLightsForStatus( )
    {
        switch ( getHardwareStatus( ) )
        {
            default:
            case HARDWARE_OFF:
                // Off.
                Lights::setNeopix( HARDWARE_PIXEL, 0, 0, 0 );
                break;

            case HARDWARE_BOOTING:
                // Blue.
                Lights::setNeopix( HARDWARE_PIXEL, 0, 0, MAX_BRIGHTNESS );
                break;

            case HARDWARE_ERRORS:
                // Red.
                Lights::setNeopix( HARDWARE_PIXEL, MAX_BRIGHTNESS, 0, 0 );
                break;

            case HARDWARE_WARNINGS:
                // Red.
                Lights::setNeopix( HARDWARE_PIXEL, MAX_BRIGHTNESS/2, MAX_BRIGHTNESS/2, 0 );
                break;

            case HARDWARE_READY:
                // Green.
                Lights::setNeopix( HARDWARE_PIXEL, 0, MAX_BRIGHTNESS, 0 );
                break;
        }

        switch ( getSoftwareStatus( ) )
        {
            default:
            case SOFTWARE_OFF:
                // Off.
                Lights::setNeopix( SOFTWARE_PIXEL, 0, 0, 0 );
                break;

            case SOFTWARE_BOOTING:
                // Blue.
                Lights::setNeopix( SOFTWARE_PIXEL, 0, 0, MAX_BRIGHTNESS );
                break;

            case SOFTWARE_ERRORS:
                // Red.
                Lights::setNeopix( SOFTWARE_PIXEL, MAX_BRIGHTNESS, 0, 0 );
                break;

            case SOFTWARE_READY:
            case SOFTWARE_RUNNING:
                // Green.
                Lights::setNeopix( SOFTWARE_PIXEL, 0, MAX_BRIGHTNESS, 0 );
                break;
        }

        switch ( getCameraStatus( ) )
        {
            default:
            case CAMERA_OFF:
                // Off.
                Lights::setNeopix( CAMERA_PIXEL, 0, 0, 0 );
                break;

            case CAMERA_BOOTING:
                // Blue.
                Lights::setNeopix( CAMERA_PIXEL, 0, 0, MAX_BRIGHTNESS );
                break;

            case CAMERA_READY:
                // Green.
                Lights::setNeopix( CAMERA_PIXEL, 0, MAX_BRIGHTNESS, 0 );
                break;

            case CAMERA_SHOOTING:
                // White.
                Lights::setNeopix( CAMERA_PIXEL, MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS );
                break;
        }
    }
};
