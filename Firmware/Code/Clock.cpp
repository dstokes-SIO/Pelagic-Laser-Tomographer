#include "Clock.h"

RTC_DS3231 Clock::rtc;
bool Clock::initialized = false;





//----------------------------------------------------------------------
// Get and Set.
//----------------------------------------------------------------------
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
bool Clock::parseDate(
    const char*const string,
    uint16_t& year,
    uint8_t& month,
    uint8_t& day,
    uint8_t& hour,
    uint8_t& minute,
    uint8_t& second )
{
    const char* s = string;
    bool yearFirst = true;
    uint16_t tmp = 0;

    // Initialize.
    year = 2000;
    month = day = 1;
    hour = minute = second = 0;

    // Skip to digits, parse year.
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Nothing given.
    year = atoi( s );

    // Skip to next digits, parse month.
    while ( *s != '\0' && isDigit(*s) )
        ++s;
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Year only given.
    tmp = atoi( s );

    // Decide if we have year first.
    if ( year > 31 )
    {
        // Year first. Next two numbers are month and day.
        month = tmp;
    }
    else
    {
        // Year not first. Assumed to be month, day, year.
        yearFirst = false;
        month = year;
        day = tmp;
        year = 2000;
    }

    // Skip to next digits, parse day.
    while ( *s != '\0' && isDigit(*s) )
        ++s;
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Only Year and month, or month and day.
    tmp = atoi( s );

    if ( yearFirst )
        day = tmp;
    else
        year = tmp;

    // Skip to next digits, parse hour.
    while ( *s != '\0' && isDigit(*s) )
        ++s;
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Only date.
    hour = atoi( s );

    // Skip to next digits, parse minute.
    while ( *s != '\0' && isDigit(*s) )
        ++s;
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Only date and hour.
    minute = atoi( s );

    // Skip to next digits, parse second.
    while ( *s != '\0' && isDigit(*s) )
        ++s;
    while ( *s != '\0' && !isDigit(*s) )
        ++s;
    if ( *s == '\0' )
        return false;   // Only date, hour, and minute.
    second = atoi( s );

    return true;    // Date, hour, minute, and second.
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
 * @see isPresent()
 * @see now()
 */
const char* Clock::nowString( const uint8_t format )
{
    static char s[25];

    if ( !initialized )
    {
        // The clock was not found, so it is not possible to return a
        // current date and time. Instead, use the built-in clock to
        // get a time since the most recent boot. Use that to create
        // a fake time that is suitable for relative timestamping.
        DateTime dt( SECONDS_FROM_1970_TO_2000 + millis( ) / 1000 );
        switch ( format )
        {
            case TIME_ISO8601:
                return dt.timestamp( DateTime::TIMESTAMP_FULL ).c_str( );

            case TIME_RFC3339:
                strcpy( s, "YYYY-MM-DD hh:mm:ss" );
                return dt.toString( s );

            default:
            case TIME_EXCEL:
                strcpy( s, "MM/DD/YYYY hh:mm:ss" );
                return dt.toString( s );
        }
    }

    switch ( format )
    {
        case TIME_ISO8601:
            return rtc.now( ).timestamp( DateTime::TIMESTAMP_FULL ).c_str( );

        case TIME_RFC3339:
            strcpy( s, "YYYY-MM-DD hh:mm:ss" );
            return rtc.now( ).toString( s );

        default:
        case TIME_EXCEL:
            strcpy( s, "MM/DD/YYYY hh:mm:ss" );
            return rtc.now( ).toString( s );
    }
}





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
 * @see isPresent()
 */
bool Clock::setDateTime(
    const uint16_t year,
    const uint8_t month,
    const uint8_t day,
    const uint8_t hour,
    const uint8_t minute,
    const uint8_t second )
{
    if ( !initialized )
        return false;

    DateTime dt( year, month, day, hour, minute, second );
    if ( !dt.isValid( ) )
    {
#if defined(DEBUG_VERBOSE_CLOCK)
        Serial.print( "Debug: Real-time clock date could not be set to invalid values.\r\n" );
#endif
        return false;
    }

    rtc.adjust( dt );
#if defined(DEBUG_VERBOSE_CLOCK)
    Serial.printf( "Debug: Real-time clock date set to %s.\r\n", nowString( ) );
#endif
    return true;
}





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
 * @see isPresent()
 */
bool Clock::setDateTime( const char*const string )
{
    if ( !initialized )
        return false;

    uint16_t year;
    uint8_t month, day, hour, minute, second;
    if ( !parseDate( string, year, month, day, hour, minute, second ) )
    {
#if defined(DEBUG_VERBOSE_CLOCK)
        Serial.print( "Debug: New real-time clock date could not be parsed.\r\n" );
#endif
        return false;
    }

    DateTime dt( year, month, day, hour, minute, second );
    if ( !dt.isValid( ) )
    {
#if defined(DEBUG_VERBOSE_CLOCK)
        Serial.print( "Debug: Real-time clock date could not be set to invalid values.\r\n" );
#endif
        return false;
    }

    rtc.adjust( dt );
#if defined(DEBUG_VERBOSE_CLOCK)
    Serial.printf( "Debug: Real-time clock date set to %s.\r\n", nowString( ) );
#endif
    return true;
}
