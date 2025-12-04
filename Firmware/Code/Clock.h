#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <RTClib.h>

#include "pltlogger.h"


/**
 * Manages the real time clock.
 *
 * The device has two clocks:
 *
 * - A built-in counter reporting the number of milliseconds since the
 *   device was last booted. This is useful for rough timing intervals.
 *
 * - An Adafruit DS3231 precision real-time clock. The clock provides a
 *   high-precision date and time that can be formatted and written to
 *   log files as a timestamp.
 *
 * @see https://www.adafruit.com/product/3013
 * @see https://github.com/adafruit/RTClib
 */
class Clock
{
private:
    Clock( ) = delete;
    Clock( const Clock& ) = delete;
    Clock& operator=( const Clock& ) = delete;


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
public:
    // Available date-time formats.
    static const uint8_t TIME_EXCEL   = 0;  // MM/DD/YYYY hh:mm:ss
    static const uint8_t TIME_RFC3339 = 1;  // YYYY-MM-DD hh:mm:ss
    static const uint8_t TIME_ISO8601 = 2;  // YYYY-MM-DDThh:mm:ss


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    // The current date and time to a second resolution.
    static RTC_DS3231 rtc;

    // A flag indicating if the clock exists and has been initialized.
    static bool initialized;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the real time clock.
     *
     * @return
     *   Returns true on success and false on failure.
     *
     * @see getClockName()
     * @see isClockPresent()
     */
    static inline bool init( )
    {
        if ( !rtc.begin( ) )
        {
#if defined(DEBUG_VERBOSE_CLOCK)
            Serial.print( "Debug: Real-time clock initialization FAIL.\r\n" );
#endif
            return false;
        }

        // The begin() method does not detect when the clock hardware
        // is not present. To try and detect this, set the clock and
        // see if the set worked, within some small delta.
        const uint32_t restoreTime = rtc.now( ).unixtime( );

        DateTime dt( (uint32_t) SECONDS_FROM_1970_TO_2000 );
        rtc.adjust( dt );

        const uint32_t setSec = dt.secondstime( );
        const uint32_t nowSec = rtc.now( ).secondstime( );
        if ( (abs(nowSec - setSec)) > 10 )
        {
#if defined(DEBUG_VERBOSE_CLOCK)
            Serial.print( "Debug: Real-time clock initialization FAIL.\r\n" );
#endif
            return false;
        }

        // Restore the time before the test.
        rtc.adjust( restoreTime );

        initialized = true;
#if defined(DEBUG_VERBOSE_CLOCK)
        Serial.print( "Debug: Real-time clock initialized.\r\n" );
#endif
        return true;
    }

    /**
     * Returns the name of the real time clock.
     *
     * @return
     *   Returns the name.
     *
     * @see init()
     * @see isClockPresent()
     */
    static inline const char* getClockName( )
    {
        // This software uses the DS3231 real time clock, but other
        // implementations may use different clock hardware. Return here
        // the name of the hardware for use in logging.
        return "DS3231 real time clock";
    }

    /**
     * Returns true if the clock is present.
     *
     * @return
     *   Returns true if present.
     *
     * @see init()
     * @see getClockName()
     */
    static inline bool isClockPresent( )
    {
        return initialized;
    }


//----------------------------------------------------------------------
// Get and Set.
//----------------------------------------------------------------------
private:
    /**
     * Parses a date string into its components.
     *
     * The string is parsed, looking for a sequence of year, month, day,
     * hour, minute, and second. Numbers may be separated by spaces,
     * letters, or punctuation. Examples:
     * - "2021 01 01 12 30 00"
     * - "2021-01-01 12:30:00"
     * - "2021/01/01 12:30.00"
     *
     * The order of year, month, and day is inferred from the numbers.
     *   - If the first number is greater than 31, it is assumed to be
     *     the year. The remaining two values in the date are assumed
     *     to be the month and day, in that order.
     *
     * @param[in] string
     *   The string to parse.
     * @param[out] year
     *   The returned year.
     * @param[out] month
     *   The returned month.
     * @param[out] day
     *   The returned day.
     * @param[out] hour
     *   The returned hour.
     * @param[out] minute
     *   The returned minute.
     * @param[out] second
     *   The returned second.
     *
     * @return
     *   Returns true if all values were found.
     *
     * @see setDateTime()
     */
    static bool parseDate(
        const char*const string,
        uint16_t& year,
        uint8_t& month,
        uint8_t& day,
        uint8_t& hour,
        uint8_t& minute,
        uint8_t& second );

public:
    /**
     * Returns the current date and time to a one second resolution.
     *
     * If the clock is not initialized (it was not found), a date time
     * with a POSIX epoch is returned.
     *
     * @return
     *   Returns a DateTime object.
     *
     * @see isClockPresent()
     * @see nowString()
     */
    static inline DateTime now( )
    {
        if ( !initialized )
            return DateTime( (uint32_t) 0 );
        return rtc.now( );
    }

    /**
     * Returns the current date and time's millisecond offset.
     *
     * The offset is the number of milliseconds since the clock's seconds
     * last ticked over. So, if the clock seconds ticked 1/2 second ago,
     * this method returns 1/2 second = 500 ms.
     *
     * This value is approximate. The real time clock itself does not
     * have millisecond resolution. Instead, this method uses the processor's
     * built-in approximate millisecond resolution clock to estimate the
     * number of milliseconds into the next second.
     *
     * @return
     *   Returns the millisecond offset after the clock's seconds last
     *   ticked over.
     */
    static inline uint32_t nowMillisOffset( )
    {
        // We arbitrarily declare that at boot time the millisecond offset
        // is zero. millis( ) returns the time, in ms, since boot. So modulo
        // that by 1000 gets the millisecond offset.
        return millis( ) % 1000;
    }

    /**
     * Returns the current date and time as an Excel, RFC-3339, or ISO-8601
     * standard formatted time.
     *
     * - The RFC-3339 standard shows year, month, day, hour, minute,
     *   and second as YYYY-MM-DD hh:mm:ss.
     *
     * - The ISO-8601 standard shows the same as YYYY-MM-DDThh:mm:ss, where
     *   the embeded "T" is really a "T". This replaces the white space
     *   of RFC-3339 to make the string parsable as a single "word".
     *
     * - Microsoft's Excel supports a variety of date/time formats, but
     *   the most common is MM/DD/YYYY hh:mm:ss. Because logged data is
     *   likely to be imported into Excel or equivalent spreadsheets,
     *   this format is the default.
     *
     * If the clock is not initialized (it was not found), a time relative
     * to Jan 1, 2000 is returned, offset by the number of seconds since
     * the device was booted.
     *
     * @param[in] format
     *   The format to use. One of TIME_EXCEL, TIME_RFC3339, or TIME_ISO8601.
     *
     * @return
     *   Returns a formatted string for the current time.
     *
     * @see isClockPresent()
     * @see now()
     */
    static const char* nowString( const uint8_t format = TIME_EXCEL );

    /**
     * Sets the current date and time.
     *
     * If the clock is not initialized (it was not found), no action
     * is taken.
     *
     * @param[in] year
     *   The year (2000-2099).
     * @param[in] month
     *   The month (1-12).
     * @param[in] day
     *   The day of the month (1-31).
     * @param[in] hour
     *   The hour (0-23).
     * @param[in] minute
     *   The minute (0-59).
     * @param[in] second
     *   The second (0-59).
     *
     * @return
     *   Returns true if the date/time is valid and set. Returns false if
     *   the clock is not initialized or the date/time has invalid values.
     *
     * @see isClockPresent()
     */
    static bool setDateTime(
        const uint16_t year,
        const uint8_t month,
        const uint8_t day,
        const uint8_t hour,
        const uint8_t minute,
        const uint8_t second = 0 );

    /**
     * Sets the current date and time.
     *
     * The string is parsed, looking for a sequence of year, month, day,
     * hour, minute, and second. Numbers may be separated by spaces,
     * letters, or punctuation.
     * - "2021 01 01 12 30 00"
     * - "2021-01-01 12:30:00"
     * - "2021/01/01 12:30.00"
     *
     * @param[in] string
     *   The date and time string.
     *
     * @return
     *   Returns true if the date/time is valid and set. Returns false if
     *   the clock is not initialized, the date/time cannot be parsed, or
     *   the date/time has invalid values.
     *
     * @see isClockPresent()
     */
    static bool setDateTime( const char*const string );
};
