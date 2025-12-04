#include "Commands.h"

#include "pltlogger.h"  // Logger.
#include "Battery.h"    // Battery.
#include "Camera.h"     // Camera and intensifier.
#include "Clock.h"      // Real time clock.
#include "Laser.h"      // Laser.
#include "Lights.h"     // Light (LEDs).
#include "Sensors.h"    // Inertial, pressure, and temperature sensors.

char Commands::lineBuffer[MAXLINE+1];
uint16_t Commands::lineBufferIndex = 0;





//----------------------------------------------------------------------
// Utilities.
//----------------------------------------------------------------------
/**
 * Formats an unsigned 64-bit integer.
 *
 * @param[in] value
 *   The value to format.
 * @return
 *   Returns a string version of the value.
 */
const char* Commands::uint64ToString( uint64_t value )
{
    static const int MAXCHARS = 20 + (20/3);    // Digits + commas
    static char digits[MAXCHARS+1];

    if ( value == 0 )
        return "0";

    digits[MAXCHARS] = '\0';

    int i;
    int d;
    for ( i = MAXCHARS - 1, d = 0; i >= 0 && value > 0; --i, ++d )
    {
        if ( d != 0 && (d%3) == 0 )
            digits[i--] = ',';
        digits[i] = '0' + (value % 10);
        value /= 10;
    }
    if ( i <= 0 )
        return digits;

    return &digits[i+1];
}





//----------------------------------------------------------------------
// Handle commands.
//----------------------------------------------------------------------
/**
 * Process pending serial input.
 *
 * The serial input is checked. If there are bytes ready to read,
 * they are read, parsed as a command, and the command executed.
 */
void Commands::handleSerialInput( )
{
    while ( Serial.available( ) > 0 )
    {
        const char c = (char) Serial.read( );
        if ( c == '\r' || c == '\n' )
        {
            // EOL. Null terminate and fall through for parsing.
            lineBuffer[lineBufferIndex] = '\0';
        }
        else if ( c == 0x08 || (uint16_t)c == 0x007F )
        {
            // Backspace or Delete. Back up if we can.
            if ( lineBufferIndex != 0 )
            {
                Serial.print( (char)0x08 );
                Serial.print( ' ' );
                Serial.print( (char)0x08 );
                --lineBufferIndex;
            }
            continue;
        }
        else if ( iscntrl( c ) )
        {
            // Control character. Ignore.
            continue;
        }
        else if ( lineBufferIndex == 0 && isSpace( c ) )
        {
            // White space. Ignore at start of a line.
            Serial.print( c );
            continue;
        }
        else if ( lineBufferIndex < MAXLINE )
        {
            // Character and room in buffer. Save it.
            Serial.print( c );
            lineBuffer[lineBufferIndex] = c;
            ++lineBufferIndex;
            continue;
        }
        else
        {
            // Character and no more room in buffer. Flush and
            // fall through for parsing.
            flushSerialInput( );
        }

        // EOL.
        Serial.println( );
        if ( lineBufferIndex > 0 )
        {
            dispatch( lineBuffer );
            lineBufferIndex = 0;
        }
        printPrompt( );
    }
}

/**
 * Flushes any pending serial input up to the next end of line.
 */
void Commands::flushSerialInput( )
{
    while ( Serial.available( ) > 0 )
    {
        const char c = (char) Serial.read( );
        if ( c == '\r' || c == '\n' )
            return;
    }
}





//----------------------------------------------------------------------
// Utilities.
//----------------------------------------------------------------------
/**
 * Parses a line into a command an optional argument.
 *
 * @param[in,out] string
 *   The line to parse. The line is modified during parsing.
 * @param[out] command
 *   The first word on the line.
 * @param[out] arg
 *   The remainder of the line after the first word and possible
 *   white space.
 */
void Commands::parseLine(
    char*const string,
    char*& command,
    char*& arg )
{
    // Skip spaces, skip to the end of the next word, and use it
    // as the command.
    char* s = string;
    while ( *s != '\0' && isSpace(*s) )
        ++s;
    char* s2 = s;
    while ( *s2 != '\0' && !isSpace(*s2) )
        ++s2;

    command = s;
    if ( *s2 == '\0' )
    {
        // Only a command. No argument.
        *s2 = '\0';
        arg = s2;
        return;
    }

    *s2 = '\0';
    s = s2 + 1;
    while ( *s != '\0' && isSpace(*s) )
        ++s;
    arg = s;
}





//----------------------------------------------------------------------
// Parse and dispatch.
//----------------------------------------------------------------------
/**
 * Dispatches the command or prints an error message if unknown.
 *
 * @param[inout] line
 *   The command line, including the command and any arguments.
 *   The string is modified during parsing.
 */
void Commands::dispatch( char*const line )
{
    char* command = nullptr;
    char* arg = nullptr;
    parseLine( line, command, arg );

    // Information.
    if ( strcmp( command, "help" ) == 0 )
    {
        help( arg );
        return;
    }

    // Status.
    if ( strcmp( command, "date" ) == 0 )
    {
        if ( *arg != '\0' )
        {
            if ( !Clock::isClockPresent( ) )
                Serial.print( "Date cannot be set. Real time clock not found.\r\n" );
            else
            {
                // Parse the rest of the line as a date and time.
                if ( !Clock::setDateTime( arg ) )
                    Serial.print( "Invalid date. Use 'date Y M D h m s'.\r\n" );
                else
                    Serial.printf( "%s\r\n", Clock::nowString( ) );
            }
        }
        else
        {
            if ( !Clock::isClockPresent( ) )
                Serial.print( "The real time clock was not found. Dates are 1/1/2000 + ms since boot.\r\n" );
            Serial.printf( "%s\r\n", Clock::nowString( ) );
        }
        return;
    }
    if ( strcmp( command, "version" ) == 0 )
    {
        Serial.printf( "%s\r\n", VERSION );
        return;
    }
    if ( strcmp( command, "hwinfo" ) == 0 )
    {
        hwinfo( );
        return;
    }
    if ( strncmp( command, "sensor", 6 ) == 0 )
    {
        sensors( );
        return;
    }
    if ( strcmp( command, "status" ) == 0 )
    {
        status( );
        return;
    }
    if ( strcmp( command, "test" ) == 0 )
    {
        if ( strcmp( arg, "lights" ) == 0 )
        {
            Serial.printf( "Testing lights...\r\n" );
            Lights::testCycle( );
        }
        else if ( strcmp( arg, "laser" ) == 0 )
        {
            Serial.printf( "Testing laser...\r\n" );
            Laser::testCycle( );
        }
        else
            help( command );

        // Restore the lights to show the current state.
        Lights::setLightsForStatus( );
        return;
    }

    // Config.
    if ( strcmp( command, "interval" ) == 0 )
    {
        if ( *arg == '\0' )
        {
            // No argument given. Show the current interval.
            Serial.printf( "%ld ms\r\n", getFrameInterval( ) );
        }
        else
        {
            const uint32_t interval = atoi( arg );
            if ( !setFrameInterval( interval ) )
                Serial.printf( "Bad interval. Use >= %ld ms or 0 to reset to default.\r\n",
                    MINIMUM_FRAME_INTERVAL );
            else if ( interval == 0 )
                Serial.printf( "Frame interval reset to default %ld ms\r\n",
                    getFrameInterval( ) );
            else
                Serial.printf( "Frame interval set to %ld ms\r\n", getFrameInterval( ) );
        }
        return;
    }
    if ( strcmp( command, "lasermode" ) == 0 )
    {
        bool showValue = true;
        if ( *arg != '\0' )
        {
            if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
            {
                Serial.print( "Cannot change laser mode while imaging is in progress.\r\n" );
                showValue = false;
            }
            else if ( strncmp( arg, "norm", 4 ) == 0 )
                setLaserContinuous( false );
            else if ( strncmp( arg, "cont", 4 ) == 0 )
                setLaserContinuous( true );
            else
            {
                Serial.printf( "Unknown mode. Use 'normal' or 'continuous'.\r\n" );
                showValue = false;
            }
        }

        if ( showValue )
        {
            if ( isLaserContinuous( ) )
                Serial.print( "Continuous. Laser will be on for the whole run.\r\n" );
            else
                Serial.print( "Normal. Laser will be turned on for each image.\r\n" );
        }
        return;
    }
    if ( strcmp( command, "burstsize" ) == 0 )
    {
        bool showValue = true;
        if ( *arg != '\0' )
        {
            if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
            {
                Serial.print( "Cannot change burst size while imaging is progress.\r\n" );
                showValue = false;
            }
            else
                setBurstSize( atoi( arg ) );
        }
        if ( showValue )
            Serial.printf( "Shoot %d images at a time.\r\n", getBurstSize( ) );
        return;
    }

    // Actions.
    if ( strcmp( command, "camera" ) == 0 )
    {
        if ( *arg != '\0' )
        {
            if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
            {
                Serial.print( "Cannot change camera on/off while imaging is in progress.\r\n" );
                return;
            }
            else if ( strcmp( arg, "on" ) == 0 )
            {
                if ( Camera::isPowerOn( ) == true )
                {
                    Serial.print( "Camera and intensifier are already on.\r\n" );
                    Serial.print( "  If this is not the case, the software is out of sync\r\n" );
                    Serial.print( "  with the camera state. Use 'camera forceoff'.\r\n" );
                    return;
                }

                setCameraStatus( CAMERA_BOOTING );
                Serial.print( "Camera and intensifier powering up...\r\n" );
                Camera::setPower( true );
                setCameraStatus( CAMERA_READY );
                Lights::setLightsForStatus( );
                Serial.printf( "Camera and intensifier are on.\r\n" );

                Serial.print( "  Beware: use 'camera off' or the software may get out of sync\r\n" );
                Serial.print( "  with the camera state. Use 'camera forceoff' if that occurs.\r\n" );
                return;
            }
            else if ( strcmp( arg, "off" ) == 0 )
            {
                if ( Camera::isPowerOn( ) == false )
                {
                    Serial.print( "Camera is already off.\r\n" );
                    Serial.print( "  If this is not the case, the software is out of sync\r\n" );
                    Serial.print( "  with the camera state. Use 'camera forceoff'.\r\n" );
                    return;
                }

                Serial.print( "Camera and intensifier powering down...\r\n" );
                Camera::setPower( false );
                setCameraStatus( CAMERA_OFF );
                Lights::setLightsForStatus( );
                Serial.printf( "Camera and intensifier are off.\r\n" );
                return;
            }
            else if ( strcmp( arg, "forceoff" ) == 0 ||
                      strcmp( arg, "reset" ) == 0 )
            {
                Serial.print( "Camera and intensifier powering down (force)...\r\n" );
                Camera::setPower( false, true );
                setCameraStatus( CAMERA_OFF );
                Lights::setLightsForStatus( );
                Serial.printf( "Camera and intensifier should be off.\r\n" );
                Serial.print( "  If the camera still appears to be on, use this command again.\r\n" );
                return;
            }
            else
            {
                Serial.printf( "Unknown camera command: %s\r\n", arg );
                Serial.print( "Use 'on', 'off', or 'forceoff'.\r\n" );
                return;
            }
        }

        Serial.printf( "Camera is %s.\r\n",
            (Camera::isPowerOn() == true) ? "on" : "off" );
        return;
    }
    if ( strcmp( command, "laser" ) == 0 )
    {
        if ( *arg != '\0' )
        {
            if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
            {
                Serial.print( "Cannot change laser on/off while imaging is in progress.\r\n" );
                return;
            }
            else if ( strcmp( arg, "on" ) == 0 )
            {
                Serial.print( "Laser powering up...\r\n" );
                Laser::setPower( true );
                Serial.printf( "Laser is on.\r\n" );
                return;
            }
            else if ( strcmp( arg, "off" ) == 0 )
            {
                Serial.print( "Laser powering down...\r\n" );
                Laser::setPower( false );
                Serial.printf( "Laser is off.\r\n" );
                return;
            }
            else
            {
                Serial.printf( "Unknown laser command: %s\r\n", arg );
                Serial.print( "Use 'on' or 'off'.\r\n" );
                return;
            }
        }

        Serial.printf( "Laser is %s.\r\n",
            (Laser::isPowerOn() == true) ? "on" : "off" );
        return;
    }
    if ( strcmp( command, "reset" ) == 0 )
    {
        reset( );
        return;
    }
    if ( strcmp( command, "start" ) == 0 )
    {
        start( );
        return;
    }
    if ( strcmp( command, "stop" ) == 0 )
    {
        stop( );
        return;
    }
    if ( strcmp( command, "snap" ) == 0 )
    {
        if ( *arg == '\0' )
            snap( getBurstSize( ) );
        else
        {
            const uint16_t nImages = atoi( arg );
            if ( nImages <= 1 )
                snap( 1 );
            else
                snap( nImages );
        }
        return;
    }

    // Files.
    if ( strcmp( command, "cat" ) == 0 )
    {
        if ( *arg == '\0' )
            help( command );
        else if ( !FileSystem::cat( arg ) )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        return;
    }
    if ( strcmp( command, "du" ) == 0 )
    {
        uint64_t nBytes = 0;
        if ( *arg == '\0' )
            nBytes = FileSystem::du( "/" );
        else
            nBytes = FileSystem::du( arg );
        if ( nBytes == 0 && FileSystem::hasError( ) )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        else
            Serial.printf( "%s bytes\r\n", uint64ToString( nBytes ) );
        return;
    }
    if ( strcmp( command, "format" ) == 0 )
    {
        if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
        {
            Serial.print( "Cannot format SD card while imaging is in progress.\r\n" );
            Serial.print( "Type 'stop' first.\r\n" );
            return;
        }

        flushSerialInput( );
        Serial.print( "Formatting will delete all SD card files.\r\n" );
        Serial.print( "Are you sure (y|n)? " );

        while ( !Serial.available( ) ) {
            yield( );
        }
        const int nBytes = Serial.readBytesUntil( '\n', lineBuffer, MAXLINE );
        lineBuffer[nBytes] = '\0';
        Serial.println( lineBuffer );

        if ( nBytes > 0 && (lineBuffer[0] == 'y' || lineBuffer[0] == 'Y') )
        {
            FileSystem::format( );
            reset( );
        }
        else
            Serial.print( "Format canceled.\r\n" );
        return;
    }
    if ( strcmp( command, "head" ) == 0 )
    {
        if ( *arg == '\0' )
            help( command );
        else if ( !FileSystem::head( arg ) )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        return;
    }
    if ( strcmp( command, "ls" ) == 0 )
    {
        bool status = false;
        if ( arg[0] == '\0' )
            status = FileSystem::ls( "/" );
        else
            status = FileSystem::ls( arg );
        if ( !status )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        return;
    }
    if ( strcmp( command, "rm" ) == 0 )
    {
        if ( *arg == '\0' )
            help( command );
        else if ( !FileSystem::rmall( arg ) )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        return;
    }
    if ( strcmp( command, "tail" ) == 0 )
    {
        if ( *arg == '\0' )
            help( command );
        else if ( !FileSystem::tail( arg ) )
        {
            FileSystem::printErrorMessage( );
            updateStatus( );
        }
        return;
    }

    Serial.printf( "Unknown command: %s\r\n", command );
    Serial.print( "Type 'help' for a list of commands.\r\n" );
}





//----------------------------------------------------------------------
// Information.
//----------------------------------------------------------------------
/**
 * Prints command help to the serial port.
 *
 * @param[in] arg
 *   The help argument, if any.
 */
void Commands::help( const char*const arg )
{
    static const int HELP_LINES = 8;
    static const char*const col1[] = {
        "Info:",
        "  help [COMMAND]",
        "  hwinfo",
        "  sensors",
        "  status",
        "  version",
        "",
        "",
    };
    static const char*const col2[] = {
        "Settings:",
        "  burstsize [N]",
        "  date [DT]",
        "  interval [N]",
        "  lasermode [MODE]",
        "",
        "",
        "",
    };
    static const char*const col3[] = {
        "Actions:",
        "  camera [STATE]",
        "  laser [STATE]",
        "  reset",
        "  snap [N]",
        "  start",
        "  stop",
        "  test NAME",
    };
    static const char*const col4[] = {
        "Files:",
        "  cat PATH",
        "  du [PATH]",
        "  format",
        "  head PATH",
        "  ls [PATH]",
        "  rm PATH",
        "  tail PATH",
    };

    if ( *arg == '\0' )
    {
        for ( int i = 0; i < HELP_LINES; ++i )
        {
            Serial.printf( "%-18s%-18s%-18s%-18s\r\n",
                col1[i], col2[i], col3[i], col4[i] );
        }
        return;
    }

    if ( strcmp( arg, "help" ) == 0 )
    {
        Serial.print( "Usage: help [CONMAND]\r\n" );
        Serial.print( "Show help on a specific COMMAND, or a list of all commands.\r\n" );
        return;
    }
    if ( strcmp( arg, "cat" ) == 0 )
    {
        Serial.print( "Usage: cat PATH\r\n" );
        Serial.print( "Show the entire contents of a file.\r\n" );
        return;
    }
    if ( strcmp( arg, "camera" ) == 0 )
    {
        Serial.print( "Usage: camera [on|off|forceoff]\r\n" );
        Serial.print( "Turn on/off the camera and intensifier.\r\n" );
        Serial.print( "Use 'forceoff' to turn off the camera and intensifier even if the\r\n" );
        Serial.print( "software thinks they are already off.\r\n" );
        return;
    }
    if ( strcmp( arg, "laser" ) == 0 )
    {
        Serial.print( "Usage: laser [on|off]\r\n" );
        Serial.print( "Turn on/off the laser.\r\n" );
        return;
    }
    if ( strcmp( arg, "date" ) == 0 )
    {
        Serial.print( "Usage: date [DT]\r\n" );
        Serial.print( "Show the date and time, or set with MM/DD/YYYY hh:mm::ss\r\n" );
        Serial.print( "(e.g. 1/20/2021 12:30:01)\r\n" );
        return;
    }
    if ( strcmp( arg, "du" ) == 0 )
    {
        Serial.print( "Usage: du [PATH]\r\n" );
        Serial.print( "Show file or directory disk usage (default to '/').\r\n" );
        return;
    }
    if ( strcmp( arg, "format" ) == 0 )
    {
        Serial.print( "Usage: format\r\n" );
        Serial.print( "Format the SD card. Prompts for confirmation.\r\n" );
        return;
    }
    if ( strcmp( arg, "head" ) == 0 )
    {
        Serial.print( "Usage: head PATH\r\n" );
        Serial.print( "Show the first 10 lines of a file.\r\n" );
        return;
    }
    if ( strcmp( arg, "hwinfo" ) == 0 )
    {
        Serial.print( "Usage: hwinfo\r\n" );
        Serial.print( "Show memory and SD card use, and what hardware is working.\r\n" );
        return;
    }
    if ( strcmp( arg, "interval" ) == 0 )
    {
        Serial.print( "Usage: interval [N]\r\n" );
        Serial.print( "Show the frame interval, or set with N in ms.\r\n" );
        return;
    }
    if ( strcmp( arg, "ls" ) == 0 )
    {
        Serial.print( "Usage: ls [PATH]\r\n" );
        Serial.print( "Show a directory list (default to '/').\r\n" );
        return;
    }
    if ( strcmp( arg, "reset" ) == 0 )
    {
        Serial.print( "Usage: reset\r\n" );
        Serial.print( "Stop, turn off the camera and laser, close the log, and reset lights.\r\n" );
        return;
    }
    if ( strcmp( arg, "rm" ) == 0 )
    {
        Serial.print( "Usage: rm PATH\r\n" );
        Serial.print( "Remove a file or directory, recursively.\r\n" );
        Serial.print( "Use 'rm /' to remove all files.\r\n" );
        return;
    }
    if ( strcmp( arg, "sensors" ) == 0 )
    {
        Serial.print( "Usage: sensors\r\n" );
        Serial.print( "Show current sensor readings.\r\n" );
        return;
    }
    if ( strcmp( arg, "snap" ) == 0 )
    {
        Serial.print( "Usage: snap [N]\r\n" );
        Serial.print( "Snap one image or N images in a burst.\r\n" );
        return;
    }
    if ( strcmp( arg, "lasermode" ) == 0 )
    {
        Serial.print( "Usage: lasermode [MODE]\r\n" );
        Serial.print( "Show or set the laser mode to:\r\n" );
        Serial.print( "  'normal': turn laser on and off for each shot or burst.\r\n" );
        Serial.print( "  'continuous': turn laser on for entire run.\r\n" );
        return;
    }
    if ( strcmp( arg, "burstsize" ) == 0 )
    {
        Serial.print( "Usage: burstsize [N]\r\n" );
        Serial.print( "Show the burst size or set it to N frames.\r\n" );
        return;
    }
    if ( strcmp( arg, "start" ) == 0 )
    {
        Serial.print( "Usage: start\r\n" );
        Serial.print( "Start running, snapping images and logging.\r\n" );
        return;
    }
    if ( strcmp( arg, "status" ) == 0 )
    {
        Serial.print( "Usage: status\r\n" );
        Serial.print( "Show current running status.\r\n" );
        return;
    }
    if ( strcmp( arg, "stop" ) == 0 )
    {
        Serial.print( "Usage: stop\r\n" );
        Serial.print( "Stop running.\r\n" );
        return;
    }
    if ( strcmp( arg, "tail" ) == 0 )
    {
        Serial.print( "Usage: tail PATH\r\n" );
        Serial.print( "Show the last 10 lines of a file.\r\n" );
        return;
    }
    if ( strcmp( arg, "test" ) == 0 )
    {
        Serial.print( "Usage: test NAME\r\n" );
        Serial.print( "Run a 'laser' or 'lights' hardware test.\r\n" );
        return;
    }
    if ( strcmp( arg, "version" ) == 0 )
    {
        Serial.print( "Usage: version\r\n" );
        Serial.print( "Show the software version.\r\n" );
        return;
    }
    Serial.printf( "help: Unknown command: %s\r\n", arg );
    Serial.print( "Type 'help' for a list of commands.\r\n" );
}





//----------------------------------------------------------------------
// Status.
//----------------------------------------------------------------------
/**
 * Prints hardware info to the serial port.
 */
void Commands::hwinfo( )
{
    Serial.printf( "Version %s\r\n", VERSION );

    FileSystem::isCardPresent();
#ifdef HWINFO_EXTRA
    // USB_PRODUCT and USB_MANUFACTURER are normally defined by the compiler.
#if defined(USB_PRODUCT)
    Serial.printf( "  %-20s %s\r\n",
        "Processor",
        USB_PRODUCT );
#endif

#if defined(USB_MANUFACTURER)
    Serial.printf( "  %-20s %s\r\n",
        "Manufacturer",
        USB_MANUFACTURER );
#endif
#endif

    // Memory.
    //
    // RAMSIZE is usually defined by the compiler. If defined, print
    // the size, in bytes, and amount of heap space in use.
    //
    // If RAMSIZE is not defined, then just print the heap break point.
    Serial.printf( "Memory:\r\n" );
#if defined(RAMSIZE)
    Serial.printf( "  %-20s %s bytes\r\n",
        "Capacity",
        uint64ToString( RAMSIZE ) );

    const uint32_t memoryInUse = RAMSIZE - getFreeHeapMemory( );
    const float memoryPercent = 100.0 *
        ((double)memoryInUse) / ((double)RAMSIZE);

    Serial.printf( "  %-20s %s bytes (%0.2f%%)\r\n",
        "Heap in use",
        uint64ToString( memoryInUse ),
        memoryPercent );
#else
    Serial.printf( "  %-20s %ld bytes\r\n",
        "Free heap",
        getFreeHeapMemory( ) );
#endif

    // Storage card.
    //
    // If the card library has initialized and the card is present,
    // then report the card type, partition type, and card capacity.
    // Recursively walk the file system to get the current space used.
    //
    // If the card libary is not initialized or the card is not present,
    // print error messages.
    Serial.printf( "SD card:\r\n" );
    if ( !FileSystem::isCardPresent( ) )
        Serial.printf( "  %-20s ** %s\r\n",
            "Format", FileSystem::getErrorMessage( ) );
    else
    {
        switch ( FileSystem::getFatType( ) )
        {
            case 16:
            case 32:
                Serial.printf( "  %-20s FAT%d\r\n",
                    "Format",
                    FileSystem::getFatType( ) );
                break;
            default:
                Serial.printf( "  %-20s ** Unknown\r\n", "Format" );
                break;
        }

        const uint64_t sdcardCapacity = FileSystem::getCardCapacity( );
        Serial.printf( "  %-20s %s bytes\r\n",
            "Capacity",
            uint64ToString( sdcardCapacity ) );

        const uint64_t sdcardInUse = FileSystem::getSpaceUsed( );
        Serial.printf( "  %-20s %s bytes (%0.3f%%)\r\n",
            "In use",
            uint64ToString( sdcardInUse ),
            FileSystem::getSpaceUsedPercent( ) );
    }

    // Components (sensors).
    Serial.print( "Components:\r\n" );
    Serial.printf( "  %-20s %s\r\n",
        "Lights",
        Lights::getLightString( ) );

    if ( !Battery::isMainPresent( ) )
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Main battery",
            Battery::getMainMonitorName( ) );
    else
    {
        const float volts = Battery::getMainVoltage( );
        const float percent = Battery::getMainPercent( );
        Serial.printf( "  %-20s %f%% (%f volts) %s\r\n",
            "Main battery",
            percent,
            volts,
            (percent < BATTERY_ERROR_PERCENT ? "** Critically low" :
             (percent < BATTERY_WARN_PERCENT ? "** Low" : "")) );
    }

    if ( !Battery::isControllerPresent( ) )
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Controller battery",
            Battery::getControllerMonitorName( ) );
    else
    {
        const float volts = Battery::getControllerVoltage( );
        const float percent = Battery::getControllerPercent( );
        Serial.printf( "  %-20s %f%% (%f volts) %s\r\n",
            "Controller battery",
            percent,
            volts,
            (percent < BATTERY_ERROR_PERCENT ? "** Critically low" :
             (percent < BATTERY_WARN_PERCENT ? "** Low" : "")) );
    }

    if ( Sensors::isInertiaSensorPresent( ) )
        Serial.printf( "  %-20s Ready\r\n",
            "Inertia module" );
    else
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Inertia module",
            Sensors::getInertiaSensorName( ) );

    if ( Sensors::isPressureSensorPresent( ) )
        Serial.printf( "  %-20s Ready\r\n",
            "Pressure sensor" );
    else
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Pressure sensor",
            Sensors::getPressureSensorName( ) );

    if ( Sensors::isTemperatureSensorPresent( ) )
        Serial.printf( "  %-20s Ready\r\n",
            "Temperature sensor" );
    else
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Temperature sensor",
            Sensors::getTemperatureSensorName( ) );

    if ( Clock::isClockPresent( ) )
    {
        Serial.printf( "  %-20s %s\r\n",
            "Real time clock",
            Clock::nowString( ) );
        Serial.printf( "    %-18s %s\r\n",
            "Date",
            Clock::nowString( ) );
        Serial.print( "    Reminder: verify the correct date and time.\r\n" );
        Serial.print( "    Type 'date Y/M/D h:m:s' to set.\r\n" );
    }
    else
    {
        Serial.printf( "  %-20s ** %s not found\r\n",
            "Real time clock",
            Clock::getClockName( ) );
        Serial.printf( "    %-18s %s\r\n",
            "Date",
            Clock::nowString( ) );
        Serial.print( "    Reminder: with no clock, dates are 1/1/2000 + ms since boot.\r\n" );
        Serial.print( "    Type 'date Y/M/D h:m:s' to set.\r\n" );
    }
}





/**
 * Prints run status to the serial port.
 */
void Commands::status( )
{
    // Hardware and software errors.
    if ( getHardwareStatus( ) == HARDWARE_BOOTING ||
         getSoftwareStatus( ) == SOFTWARE_BOOTING )
    {
        Serial.print( "Still booting. Not yet ready.\r\n" );
        return;
    }
    if ( getHardwareStatus( ) == HARDWARE_ERRORS ||
         getSoftwareStatus( ) == SOFTWARE_ERRORS )
    {
        Serial.print( "Not ready due to critical hardware errors.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
    }

    if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
        Serial.print( "Running (imaging and logging in progress).\r\n" );
    else if ( getHardwareStatus( ) == HARDWARE_WARNINGS )
    {
        Serial.print( "Ready, but there are problems that limit some activity.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
    }
    else
        Serial.print( "Ready.\r\n" );

#if defined(ENABLE_USAGE_TRACKING)
    // Usage tracking.
    Serial.print( "Usage:\r\n" );
    Serial.printf( "  %-20s %ld boots, %ld seconds powered on, %d events logged\r\n",
        "Device",
        usage.numberOfBoots,
        usage.controllerUptimeSeconds,
        usage.numberOfEventsLogged );
    Serial.printf( "  %-20s %ld boots, %ld seconds powered on, %d images shot\r\n",
        "Camera",
        Camera::getNumberOfPowerOns( ),
        Camera::getUptimeSeconds( ),
        usage.numberOfImagesSnapped );
    Serial.printf( "  %-20s %ld boots, %ld seconds powered on\r\n",
        "Laser",
        Laser::getNumberOfPowerOns( ),
        Laser::getUptimeSeconds( ) );
#endif

    // Settings.
    Serial.print( "Settings:\r\n" );
    Serial.printf( "  %-20s %d images\r\n",
        "Burst size",
        getBurstSize( ) );

    Serial.printf( "  %-20s %ld ms\r\n",
        "Image interval",
        getFrameInterval( ) );
    if ( isLaserContinuous( ) )
        Serial.printf( "  %-20s Continuous. Laser on for whole run.\r\n",
            "Laser mode" );
    else
        Serial.printf( "  %-20s Normal. Laser turned on for each shot or burst.\r\n",
            "Laser mode" );

    // Device state.
    Serial.print( "State:\r\n" );
    if ( Clock::isClockPresent( ) )
        Serial.printf( "  %-20s %s\r\n",
            "Date",
            Clock::nowString( ) );
    else
        Serial.printf( "  %-20s %s (clock not found)\r\n",
            "Date",
            Clock::nowString( ) );

    Serial.printf( "  %-20s %s\r\n",
        "Laser power",
        (Laser::isPowerOn( ) ? "on" : "off") );

    Serial.printf( "  %-20s %s\r\n",
        "Camera power",
        (Camera::isPowerOn( ) ? "on" : "off") );

    if ( getSoftwareStatus( ) != SOFTWARE_RUNNING )
        Serial.printf( "  %-20s off\r\n",
            "Logging" );
    else
    {
        Serial.printf( "  %-20s %s\r\n",
            "Logging to",
            FileSystem::getDataLogFilename( ) );

        Serial.printf( "  %-20s %ld\r\n",
            "Log entries",
            FileSystem::getNumberOfDataLogEntries( ) );
    }
}





/**
 * Prints current sensor readings to the serial port.
 */
void Commands::sensors( )
{
    if ( !Sensors::isInitialized( ) )
        Serial.printf( "Some sensors not found. Values may not be valid.\r\n" );

    float pressure = 0.0;
    float depth = 0.0;
    float waterTemperature = 0.0;
    float deviceTemperature = 0.0;
    float accel[3];
    float mag[3];
    float gyro[3];

    Sensors::getWaterPressure( pressure, depth );
    Sensors::getWaterTemperature( waterTemperature );
    Sensors::getInertia( accel, mag, gyro, deviceTemperature );

    Serial.printf( "  %-20s %f mbar\r\n",
        "Pressure",
        pressure );

    Serial.printf( "  %-20s %f m\r\n",
        "Depth",
        depth );

    Serial.printf( "  %-20s %f C\r\n",
        "Water temp",
        waterTemperature );

    Serial.printf( "  %-20s %f C\r\n",
        "Device temp",
        deviceTemperature );

    Serial.printf( "  %-20s %f x %f x %f g\r\n",
        "Accelerometer",
        accel[0], accel[1], accel[2] );

    Serial.printf( "  %-20s %f x %f x %f g\r\n",
        "Magnetometer",
        mag[0], mag[1], mag[2] );

    Serial.printf( "  %-20s %f x %f x %f dps\r\n",
        "Gyroscope",
        gyro[0], gyro[1], gyro[2] );
}





//----------------------------------------------------------------------
// Actions.
//----------------------------------------------------------------------
/**
 * Snaps a photo, if the device is not imaging.
 *
 * @param[in] nImages
 *   The number of images in a burst.
 */
void Commands::snap( const uint16_t nImages )
{
    if ( getHardwareStatus( ) == HARDWARE_BOOTING ||
         getSoftwareStatus( ) == SOFTWARE_BOOTING )
    {
        Serial.print( "Still booting. Not yet ready to run.\r\n" );
        return;
    }
    if ( getHardwareStatus( ) == HARDWARE_ERRORS ||
         getSoftwareStatus( ) == SOFTWARE_ERRORS )
    {
        Serial.print( "Cannot snap due to critical hardware errors.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
        return;
    }
    if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
    {
        Serial.print( "Cannot snap a photo while imaging is in progress.\r\n" );
        return;
    }

    Serial.print( "Camera powering up...\r\n" );
    snapAndLog( nImages );

    if ( nImages == 1 )
        Serial.print( "One image shot.\r\n" );
    else
        Serial.printf( "%d images shot.\r\n", nImages );
}





/**
 * Starts the device imaging.
 */
void Commands::start( )
{
    if ( getHardwareStatus( ) == HARDWARE_BOOTING ||
         getSoftwareStatus( ) == SOFTWARE_BOOTING )
    {
        Serial.print( "Still booting. Not yet ready.\r\n" );
        return;
    }
    if ( getHardwareStatus( ) == HARDWARE_ERRORS ||
         getSoftwareStatus( ) == SOFTWARE_ERRORS )
    {
        Serial.print( "Cannot start due to critical hardware errors.\r\n" );
        Serial.print( "Type 'hwinfo' for hardware info.\r\n" );
        return;
    }
    if ( getSoftwareStatus( ) == SOFTWARE_RUNNING )
    {
        Serial.print( "Device is already started and capturing images.\r\n" );
        return;
    }

    startRunning( );
}





/**
 * Stops the device from imaging.
 */
void Commands::stop( )
{
    if ( getHardwareStatus( ) == HARDWARE_BOOTING ||
         getSoftwareStatus( ) == SOFTWARE_BOOTING )
    {
        Serial.print( "Still booting. Not yet ready.\r\n" );
        return;
    }
    if ( getSoftwareStatus( ) != SOFTWARE_RUNNING )
    {
        Serial.print( "Device is already stopped.\r\n" );
        return;
    }

    stopRunning( );
}
