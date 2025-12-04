#pragma once
#include <Arduino.h>
#include "pltlogger.h"
#include "FileSystem.h"

extern "C" char* sbrk( int incr );


/**
 * Handles serial port commands.
 *
 * Commands are read from the serial port, terminated by a carriage return.
 * The command is parsed into a single command word and an optional
 * argument. Commands that print status values or report on device state
 * are implemented here. All other commands, such as those to operate on
 * files, lights, or the camera, are implemented in appropriate classes.
 */
class Commands
{
//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
private:
    // The maximum number of characters allowed on a line.
    static const uint16_t MAXLINE = 1023;


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    static char lineBuffer[MAXLINE+1];
    static uint16_t lineBufferIndex;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes command handling.
     */
    static inline void init( )
    {
        lineBufferIndex = 0;
    }


//----------------------------------------------------------------------
// Handle commands.
//----------------------------------------------------------------------
public:
    /**
     * Process pending serial input.
     *
     * The serial input is checked. If there are bytes ready to read,
     * they are read, parsed as a command, and the command executed.
     */
    static void handleSerialInput( );

    /**
     * Flushes any pending serial input up to the next end of line.
     */
    static void flushSerialInput( );

    /**
     * Prints a command prompt on the serial port.
     */
    static inline void printPrompt( )
    {
        Serial.printf( "PLT > " );
    }


//----------------------------------------------------------------------
// Utilities.
//----------------------------------------------------------------------
private:
    /**
     * Returns the amount of free heap memory, in bytes.
     *
     * The number of bytes between the top of the stack and the heap
     * break is computed and returns as the amount of free memory
     * remaining.
     *
     * @return
     *   Returns the amount of free memory, in bytes.
     */
    static inline uint32_t getFreeHeapMemory( )
    {
        char stackTop;
        return &stackTop - reinterpret_cast<const char*>(sbrk(0));
    }

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
    static void parseLine( char*const string, char*& command, char*& arg );

    /**
     * Formats an unsigned 64-bit integer.
     *
     * @param[in] value
     *   The value to format.
     * @return
     *   Returns a string version of the value.
     */
    static const char* uint64ToString( uint64_t value );

    /**
     * Updates the device status based upon a current condition.
     */
    static inline void updateStatus( )
    {
        switch ( FileSystem::getErrorCode( ) )
        {
            case FileSystem::FS_ERROR_NOCARD:
                setHardwareStatus( HARDWARE_ERRORS );
                setSoftwareStatus( SOFTWARE_ERRORS );
                return;

            case FileSystem::FS_ERROR_BAD_FORMAT:
            case FileSystem::FS_ERROR_CARD_FULL:
                setSoftwareStatus( SOFTWARE_ERRORS );
                return;

             default:
                if ( FileSystem::isCardPresent( ) == false )
                {
                    setHardwareStatus( HARDWARE_ERRORS );
                    setSoftwareStatus( SOFTWARE_ERRORS );
                }
                return;
        }
    }


//----------------------------------------------------------------------
// Parse and dispatch.
//----------------------------------------------------------------------
private:
    /**
     * Dispatches the command or prints an error message if unknown.
     *
     * @param[inout] line
     *   The command line, including the command and any arguments.
     *   The string is modified during parsing.
     */
    static void dispatch( char*const line );


//----------------------------------------------------------------------
// Help.
//----------------------------------------------------------------------
private:
    /**
     * Prints command help to the serial port.
     *
     * @param[in] arg
     *   The help argument, if any.
     */
    static void help( const char*const arg );


//----------------------------------------------------------------------
// Status.
//----------------------------------------------------------------------
public:
    /**
     * Prints hardware info to the serial port.
     */
    static void hwinfo( );

    /**
     * Prints run status to the serial port.
     */
    static void status( );

    /**
     * Prints current sensor readings to the serial port.
     */
    static void sensors( );


//----------------------------------------------------------------------
// Actions.
//----------------------------------------------------------------------
private:
    /**
     * Snaps a photo, if the device is not imaging.
     *
     * @param[in] nImages
     *   The number of images in a burst.
     */
    static void snap( const uint16_t nImages = 1 );

    /**
     * Starts the device imaging.
     */
    static void start( );

    /**
     * Stops the device from imaging.
     */
    static void stop( );
};
