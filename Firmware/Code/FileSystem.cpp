#include <stdbool.h>
#include <stdint.h>

#include "pins.h"
#include "FileSystem.h"
#include "Clock.h"

#if defined(BATTERY_IN_DATA_LOG)
#include "Battery.h"
#endif


//----------------------------------------------------------------------
// Notes:
//
// This code uses the SdFat library that provides *basic* support for
// FAT16, FAT32, and ExFAT file systems. Presumably to keep SdFat code
// small, it has a number of limits:
//
// - SdFat has minimal error reporting. For instance, an open() can fail
//   for a number of reasons, all of which cause the method to return
//   false but without any error code indicating the problem. Similarly,
//   write() methods can fail with nothing more than a write error
//   indicated, but no cause. This minimal error reporting causes the
//   code here to try and guess the problem.
//
// - SdFat is not fast. As the number of files increases, file operations
//   slow down drastically. As the SD card fills up, write operations
//   also slow down drastically. These performance problems cause the
//   code here to put limits on the number of files and causes other code
//   to watch for performance problems.
//
// - SdFat has a single library error code that is reset on each new
//   operation. To retain that error code for reporting, this code
//   copies it to cardErrorCode. It also maintains a separate localErrorCode
//   to indicate real or inferred errors that SdFat does not distinguish,
//   such as a missing or full SD card.
//
//----------------------------------------------------------------------


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
const char*const FileSystem::DATA_LOG_FILENAME_FORMAT = "DATA_%02d.CSV";

const char*const FileSystem::SETTINGS_FILENAME = "SETTINGS.TXT";

#if defined(ENABLE_USAGE_TRACKING)
const char*const FileSystem::USAGE_FILENAME = "USAGE.TXT";
#endif

const char*const FileSystem::STATUS_LOG_FILENAME = "STATUS.TXT";


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
SdFat FileSystem::sd;
SdFile FileSystem::logFile;
bool FileSystem::initialized = false;
char FileSystem::sharedFilename[MAX_FILENAME+1];
char FileSystem::sharedBuffer[BUFFER_SIZE];

uint8_t FileSystem::cardErrorCode = SD_CARD_ERROR_INIT_NOT_CALLED;
uint8_t FileSystem::localErrorCode = FS_ERROR_UNINITIALIZED;

uint32_t FileSystem::numberOfDataLogEntries = 0;






//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
/**
 * Initializes file system management.
 *
 * The SD card is checked to see that it is present and working.
 *
 * @return
 *   Returns true on success. On failure, false is returned and error
 *   codes are set. Possible failures:
 *   - The SD card reader is wired wrong.
 *   - The SD card pin is set wrong.
 *   - The SD card is not inserted.
 *   - The SD card's format is not FAT.
 *
 * @see format()
 * @see getErrorMessage()
 * @see isInitialized()
 */
bool FileSystem::init( )
{
    initialized    = false;
    localErrorCode = FS_ERROR_UNINITIALIZED;
    cardErrorCode  = SD_CARD_ERROR_INIT_NOT_CALLED;

    if ( !sd.begin( SDCARD_PIN, SPI_HALF_SPEED ) )
    {
        // Fail to initializ. SdFat's begin() can fail for a large number
        // of reasons, most of which have no useful error code. We are
        // forced to do some guessing on failure.
        //
        // - SD_CARD_ERROR_CMD0 is a generic error that indicates that the
        //   card reader is not responding. This is likely one of:
        //   - The SD card reader is wired wrong.
        //   - The SD card pin is set wrong.
        //   - The SD card is not inserted.
        //
        // - sectorCount <= 0 is a good indicator that the SD card is missing.
        //
        // - fatType == 0 is a good indicator that the card format is not FAT.
        localErrorCode = FS_ERROR_NONE;
        cardErrorCode  = sd.sdErrorCode( );

        if ( cardErrorCode == SD_CARD_ERROR_CMD0 )
            localErrorCode = FS_ERROR_NOCARD;
        else if ( sd.card( )->sectorCount( ) <= 0 )
            localErrorCode = FS_ERROR_NOCARD;
        else if ( sd.vol( )->fatType( ) == 0 )
            localErrorCode = FS_ERROR_BAD_FORMAT;
    }
    else if ( sd.card( )->sectorCount( ) <= 0 )
    {
        // No SD card, even though begin() succeeded.
        localErrorCode = FS_ERROR_NOCARD;
    }
    else
    {
        localErrorCode = FS_ERROR_NONE;
        cardErrorCode  = SD_CARD_ERROR_NONE;
        initialized    = true;
    }

    return initialized;
}





/**
 * Formats the SD card.
 *
 * Formatting erases all SD card content and re-initializes file
 * system management. Any open files are closed.
 *
 * @return
 *   Returns true on sucess, and false on I/O errors.
 *
 * @see init()
 * @see getErrorMessage()
 * @see isInitialized()
 */
bool FileSystem::format( )
{
    initialized    = false;
    localErrorCode = FS_ERROR_UNINITIALIZED;
    cardErrorCode  = SD_CARD_ERROR_INIT_NOT_CALLED;

    // If the format below succeeds, then this next logged message will
    // be lost when all files are deleted. Log it anyway in case the
    // format fails.
    FileSystem::writeStatus( "SD card format" );

    if ( !sd.format( &Serial ) )
    {
        // Format failed.
        localErrorCode = FS_ERROR_NONE;
        cardErrorCode  = sd.sdErrorCode( );

        const char*const message = getErrorMessage( );
        Serial.printf( "Error: %s.\r\n", message );

        // It is probably not possible to save an error message to the
        // log, but try anyway.
        FileSystem::writeStatus( "SD card format failed" );
        FileSystem::writeStatus( message );

        // On a format error, fall through and try to init SD anyway.
    }

    // Re-initialize so that SdFat caches are reset.
    init( );

    if ( initialized )
        FileSystem::writeStatus( "SD card formatted" );

    return initialized;
}





/**
 * Returns the most recent error message.
 *
 * @return
 *   Returns an error message.
 *
 * @see format()
 * @see hasError()
 * @see init()
 */
const char* FileSystem::getErrorMessage( )
{
    if ( localErrorCode != FS_ERROR_NONE )
    {
        // The local error code takes precedence over the SdFat error code.
        // This is primarily done when the SdFat error code is ambiguous.
        switch ( localErrorCode )
        {
#define FS_ERROR(e, m) case FS_ERROR_##e: return m;
            FS_ERROR_CODE_LIST
#undef FS_ERROR
            default: break;
        }
    }

    switch ( cardErrorCode )
    {
#define SD_CARD_ERROR(e, m) case SD_CARD_ERROR_##e: return m;
        SD_ERROR_CODE_LIST
#undef SD_CARD_ERROR
        default: return "Unknown error";
    }
}





//----------------------------------------------------------------------
// Utilities.
//----------------------------------------------------------------------
/**
 * Parses a line into a (name,value) pair.
 *
 * @param[in,out] string
 *   The line to parse. The line is modified during parsing.
 * @param[out] name
 *   The first word on the line.
 * @param[out] value
 *   The remainder of the line after the first word and possible
 *   white space.
 */
void FileSystem::parseLine(
    char*const string,
    char*& name,
    char*& value )
{
    // Skip spaces, skip to the end of the next word, and use it
    // as the command.
    char* s = string;
    while ( *s != '\0' && isSpace(*s) )
        ++s;
    char* s2 = s;
    while ( *s2 != '\0' && !isSpace(*s2) )
        ++s2;

    name = s;
    if ( *s2 == '\0' )
    {
        // Only a name. No value.
        *s2 = '\0';
        value = s2;
        return;
    }

    *s2 = '\0';
    s = s2 + 1;
    while ( *s != '\0' && isSpace(*s) )
        ++s;
    value = s;
}

/**
 * Reads a line from the file into the shared buffer.
 *
 * Read bytes are placed in the shared buffer and NULL terminated.
 *
 * @param[in,out] file
 *   The file to read from.
 *
 * @return
 *   Returns the number of bytes read, or 0 on failure.
 */
int16_t FileSystem::readLine( SdFile& file )
{
    uint32_t nBytes = 0;
    while ( nBytes < BUFFER_SIZE-1 && file.read( sharedBuffer+nBytes, 1 ) > 0 )
    {
        if ( sharedBuffer[nBytes] == '\r' )
            continue;
        if ( sharedBuffer[nBytes] == '\n' )
            break;
        ++nBytes;
    }
    sharedBuffer[nBytes] = '\0';
    return nBytes;
}





//----------------------------------------------------------------------
// Log file.
//----------------------------------------------------------------------
/**
 * Creates a new unique data log file.
 *
 * If there is a previous log file, it is closed.
 *
 * MAX_LOG_FILES determines the maximum number of data log files, and
 * DATA_LOG_FILENAME_FORMAT is the printf() format for log file names.
 * The number of files is intentionally limited because SD card performance
 * drops quickly as the number of files increases.
 *
 * @return
 *   Returns true on success. On failure, this false is returned and
 *   error codes are set. Possible failures:
 *   - The SD card is not inserted.
 *   - The SD card is full.
 *   - The maximum number of files in a FAT directory has been reached.
 *   - The maximum number of log files has been reached.
 *
 * @see closeDataLog()
 * @see getDataLogFilename()
 * @see getNumberOfLogEntries()
 * @see isDataLogOpen()
 * @see writeDataLog()
 * @see writeDataLogHeader()
 */
bool FileSystem::newDataLog( )
{
    if ( !initialized )
        return false;

    // Close a prior file, if any.
    logFile.close( );
    bool status            = true;
    numberOfDataLogEntries = 0;
    localErrorCode         = FS_ERROR_NONE;
    cardErrorCode          = SD_CARD_ERROR_NONE;

    // Look for the next available number for which a log file does
    // not currently exist.
    for ( uint16_t i = 0; i < MAX_LOG_FILES; ++i )
    {
        sprintf( sharedFilename, DATA_LOG_FILENAME_FORMAT, i );
        if ( !FileSystem::sd.exists( sharedFilename ) )
        {
            // Found a log number not in use. Create it and open.
            logFile.open( sharedFilename, O_WRONLY|O_CREAT );
            if ( !logFile )
            {
                // File create failed.
                cardErrorCode = sd.sdErrorCode( );
                if ( sd.card( )->sectorCount( ) <= 0 )
                    localErrorCode = FS_ERROR_NOCARD;
                status = false;
            }

            // Start the log file.
            if ( !writeDataLogHeader( ) )
            {
                // File write failed. The SD card may be full. Remove
                // the newly created file.
                sd.remove( sharedFilename );
                status = false;
            }

            return status;
        }
    }

    // Could not create a unique log file name. Too many log files.
    localErrorCode = FS_ERROR_TOO_MANY_LOG_FILES;
    return false;
}





/**
 * Write a CSV log header.
 *
 * A header line is written to the current CSV log file. Per the CSV
 * file format de facto standard, the first line of a CSV file has the
 * name of each column for the rows in the rest of the file. Non-numeric
 * values are surrounded by double-quotes.
 *
 * The header line ends with a line-feed, per POSIX/Linux/macOS
 * conventions.
 *
 * @return
 *   Returns true on success. On failure, false is returned and error codes
 *   are set. Possible failures:
 *   - The SD card is not inserted.
 *   - The SD card is full.
 *   - The file has reached the 4GB max size for FAT.
 *   - A hardware error has occurred.
 *   - An internal SdFat error has occurred.
 *
 * @see closeDataLog()
 * @see getErrorMessage()
 * @see getDataLogFilename()
 * @see getNumberOfLogEntries()
 * @see hasError()
 * @see isDataLogOpen()
 * @see newDataLog()
 * @see writeDataLog()
 */
bool FileSystem::writeDataLogHeader( )
{
    if ( isDataLogOpen( ) == false )
        return false;

#if defined(BATTERY_IN_DATA_LOG)
    sprintf( sharedBuffer,
        "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
        "Timestamp",
        "Milliseconds",
        "Pressure",
        "Depth",
        "Water_Temperature",
        "Device_Temperature",
        "Acceleration_X",
        "Acceleration_Y",
        "Acceleration_Z",
        "Magnetic_X",
        "Magnetic_Y",
        "Magnetic_Z",
        "Gyroscope_X",
        "Gyroscope_Y",
        "Gyroscope_Z",
        "Controller_Volts",
        "Controller_Percent",
        "Main_Volts",
        "Main_Percent" );
#else
    sprintf( sharedBuffer,
        "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
        "Timestamp",
        "Milliseconds",
        "Pressure",
        "Depth",
        "Water_Temperature",
        "Device_Temperature",
        "Acceleration_X",
        "Acceleration_Y",
        "Acceleration_Z",
        "Magnetic_X",
        "Magnetic_Y",
        "Magnetic_Z",
        "Gyroscope_X",
        "Gyroscope_Y",
        "Gyroscope_Z" );
#endif

    // SdFat's write() has a bug... the method returns type size_t, which
    // is unsigned, but the method returns a -1 on an error, which is not
    // possible. The -1 becomes ~0, when unsigned.
    //
    // Since a successful write returns the number of bytes written, we
    // avoid signed/unsigned issues and just check if the write returns
    // the correct number of bytes.
    //
    // write() adds data to the file, but sync() updates the file's size,
    // date, cluster pointers, and cache. We need it all so that the file
    // is always uptodate.
    //
    // On failure of write() or sync(), SdFat does not set the main error
    // code. It only sets the file's error code, and that is always marked as
    // a write error, regardless of the problem. Possible problems are:
    // - The SD card is not inserted.
    // - The SD card is full.
    // - The file has reached the 4GB max size for FAT.
    // - File is not writable.
    // - A hardware error has occurred.
    // - An internal SdFat error has occurred.
    //
    // Since we've opened the file for write, and it is very very
    // unlikely that a log file will get to 4GB, the problem is
    // probably that the SD card is full.
    const uint32_t nBytes = strlen( sharedBuffer );
    if ( logFile.write( sharedBuffer, nBytes ) != nBytes ||
         !logFile.sync( ) )
    {
        // Fail to write or sync.
        cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
        if ( sd.card( )->sectorCount( ) <= 0 )
            localErrorCode = FS_ERROR_NOCARD;
        else
            localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
        logFile.close( );
        numberOfDataLogEntries = 0;
        return false;
    }

    return true;
}





/**
 * Write a CSV log entry.
 *
 * A line is written to the current CSV log file using the given
 * timestamp and sensor values. Per the CSV file format de facto
 * standard, non-numeric values (such as the date and time) are
 * surrounded by double-quotes.
 *
 * The line ends with a line-feed, per POSIX/Linux/macOS conventions.
 *
 * @param[in] dt
 *   The date and time timestamp.
 * @param[in] ms
 *   The date and time timestamp millisecond offset.
 * @param[in] pressure
 *   The water pressure.
 * @param[in] depth
 *   The water depth.
 * @param[in] waterTemperature
 *   The water temperature.
 * @param[in] deviceTemperature
 *   The device's internal temperature.
 * @param[in] accel
 *   The 3-element array with the accelerometer vector.
 * @param[in] mag
 *   The 3-element array with the magnetometer vector.
 * @param[in] gyro
 *   The 3-element array with the gyroscpe vector.
 *
 * @return
 *   Returns true on success. On failure, false is returned and error codes
 *   are set. Possible failures:
 *   - The SD card is not inserted.
 *   - The SD card is full.
 *   - The file has reached the 4GB max size for FAT.
 *   - A hardware error has occurred.
 *   - An internal SdFat error has occurred.
 *
 * @see closeDataLog()
 * @see getErrorMessage()
 * @see getDataLogFilename()
 * @see getNumberOfLogEntries()
 * @see hasError()
 * @see isDataLogOpen()
 * @see newDataLog()
 * @see writeDataLogHeader()
 */
bool FileSystem::writeDataLog(
    const char*const dt,
    const uint32_t ms,
    const float pressure,
    const float depth,
    const float waterTemperature,
    const float deviceTemperature,
    const float* accel,
    const float* mag,
    const float* gyro )
{
    if ( isDataLogOpen( ) == false )
        return false;

#if defined(BATTERY_IN_DATA_LOG)
    sprintf( sharedBuffer,
        "\"%s\",%ld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%5.3f,%3.1f,%5.3f,%3.1f\r\n",
        dt,
        ms,
        pressure,
        depth,
        waterTemperature,
        deviceTemperature,
        accel[0],
        accel[1],
        accel[2],
        mag[0],
        mag[1],
        mag[2],
        gyro[0],
        gyro[1],
        gyro[2],
        Battery::getControllerVoltage( ),
        Battery::getControllerPercent( ),
        Battery::getMainVoltage( ),
        Battery::getMainPercent( ) );
#else
    sprintf( sharedBuffer,
        "\"%s\",%ld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\r\n",
        dt,
        ms,
        pressure,
        depth,
        waterTemperature,
        deviceTemperature,
        accel[0],
        accel[1],
        accel[2],
        mag[0],
        mag[1],
        mag[2],
        gyro[0],
        gyro[1],
        gyro[2] );
#endif

    // See comments about SdFat's write() and sync() in the body of the
    // writeDataLogHeader() method above.
    const uint32_t nBytes = strlen( sharedBuffer );
    if ( logFile.write( sharedBuffer, nBytes ) != nBytes ||
         !logFile.sync( ) )
    {
        cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
        if ( sd.card( )->sectorCount( ) <= 0 )
            localErrorCode = FS_ERROR_NOCARD;
        else
            localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
        logFile.close( );
        numberOfDataLogEntries = 0;
        return false;
    }

    ++numberOfDataLogEntries;
    return true;
}





//----------------------------------------------------------------------
// Settings file.
//----------------------------------------------------------------------
/**
 * Loads settings from a saved settings file, if any.
 *
 * @param[out] interval
 *   The frame interval, in ms.
 * @param[out] isLaserContinuous
 *   True if the laser mode is continuous, and false if normal.
 * @param[out] burstSize
 *   The number of frames to capture per recording event.
 *
 * @return
 *   Returns true if a file was read, and false no file was found or
 *   an error occurred.
 *
 * @see saveSettings()
 */
bool FileSystem::loadSettings(
    uint32_t &interval,
    bool &isLaserContinuous,
    uint8_t &burstSize )
{
    if ( !initialized )
        return false;

    // Look for a settings file.
    //
    // - If the open fails, there is no file (or there is some
    //   SD card problem). Just return false.
    //
    // - Otherwise read the file, parse the value and return it.
    SdFile file;
    file.open( SETTINGS_FILENAME, O_RDONLY );
    if ( !file )
        return false;

    // Loop over lines in the file. Each line is a name-value pair.
    char* name;
    char* value;
    while ( readLine( file ) > 0 )
    {
        // Parse the line.
        parseLine( sharedBuffer, name, value );

        // Ignore malformed lines that don't have a name and a value,
        // separated by white space.
        if ( name[0] == '\0' || value[0] == '\0' )
            continue;

        if ( strcmp( name, "interval" ) == 0 )
        {
            interval = atoi( value );
        }
        else if ( strcmp( name, "burstsize" ) == 0 )
        {
            burstSize = atoi( value );
        }
        else if ( strcmp( name, "lasercontinuous" ) == 0 )
        {
            if ( atoi( value ) == 1 )
                isLaserContinuous = true;
            else
                isLaserContinuous = false;
        }
    }
    file.close( );

    return true;
}

/**
 * Saves settings to a settings file.
 *
 * @param[in] interval
 *   The frame interval, in ms.
 * @param[in] isLaserContinuous
 *   True if the laser mode is continuous, and false if normal.
 * @param[in] burstSize
 *   The number of frames to capture per recording event.
 *
 * @return
 *   Returns true if the file was written, and false if an error
 *   occurred.
 *
 * @see loadSettings()
 */
bool FileSystem::saveSettings(
    const uint32_t interval,
    const bool isLaserContinuous,
    const uint8_t burstSize )
{
    if ( !initialized )
        return false;

    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;

    // Create or overwrite settings file.
    //
    // - If the open fails, the file could not be created. There may be an
    //   SD card problem. Just return.
    //
    // - Otherwise write the settings to the file.
    SdFile file;
    file.open( SETTINGS_FILENAME, O_WRONLY|O_CREAT );
    if ( !file )
        return false;

    // See comments about SdFat's write() and sync() in the body of the
    // writeDataLogHeader() method above.
    sprintf( sharedBuffer, "interval %ld\r\nburstsize %d\r\nlasercontinuous %d\r\n",
        interval,
        burstSize,
        (isLaserContinuous ? 1 : 0) );
    const uint32_t nBytes = strlen( sharedBuffer );
    if ( file.write( sharedBuffer, nBytes ) != nBytes ||
         !file.sync( ) )
    {
        cardErrorCode  = sd.sdErrorCode( );  // Probably NONE.
        localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
        file.close( );
        return false;
    }

    file.close( );
    return true;
}





#if defined(ENABLE_USAGE_TRACKING)
//----------------------------------------------------------------------
// Stats file.
//----------------------------------------------------------------------
/**
 * Loads usage from a saved usage tracking file, if any.
 *
 * @param[out] usage
 *   The current usage.
 *
 * @return
 *   Returns true if a file was read, and false no file was found or
 *   an error occurred.
 *
 * @see saveUsage()
 */
bool FileSystem::loadUsage( Usage& usage )
{
    if ( !initialized )
        return false;

    // Look for a usage tracking file.
    //
    // - If the open fails, there is no file (or there is some
    //   SD card problem). Just return false.
    //
    // - Otherwise read the file, parse the value and return it.
    SdFile file;
    file.open( USAGE_FILENAME, O_RDONLY );
    if ( !file )
        return false;

    // Loop over lines in the file. Each line is a name-value pair.
    char* name;
    char* value;
    while ( readLine( file ) > 0 )
    {
        // Parse the line.
        parseLine( sharedBuffer, name, value );

        // Ignore malformed lines that don't have a name and a value,
        // separated by white space.
        if ( name[0] == '\0' || value[0] == '\0' )
            continue;

        if ( strcmp( name, "numberOfBoots" ) == 0 )
        {
            usage.numberOfBoots = atol( value );
        }
        else if ( strcmp( name, "numberOfCameraBoots" ) == 0 )
        {
            usage.numberOfCameraBoots = atol( value );
        }
        else if ( strcmp( name, "numberOfLaserBoots" ) == 0 )
        {
            usage.numberOfLaserBoots = atol( value );
        }
        else if ( strcmp( name, "numberOfEventsLogged" ) == 0 )
        {
            usage.numberOfEventsLogged = atol( value );
        }
        else if ( strcmp( name, "numberOfImagesSnapped" ) == 0 )
        {
            usage.numberOfImagesSnapped = atol( value );
        }
        else if ( strcmp( name, "controllerUptimeSeconds" ) == 0 )
        {
            usage.controllerUptimeSeconds = atol( value );
        }
        else if ( strcmp( name, "cameraUptimeSeconds" ) == 0 )
        {
            usage.cameraUptimeSeconds = atol( value );
        }
        else if ( strcmp( name, "laserUptimeSeconds" ) == 0 )
        {
            usage.laserUptimeSeconds = atol( value );
        }
    }
    file.close( );

    return true;
}

/**
 * Saves usage to a usage tracking file.
 *
 * @param[in] usage
 *   The current usage.
 *
 * @return
 *   Returns true if the file was written, and false if an error
 *   occurred.
 *
 * @see loadUsage()
 */
bool FileSystem::saveUsage( const Usage& usage )
{
    if ( !initialized )
        return false;

    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;

    // Create or overwrite settings file.
    //
    // - If the open fails, the file could not be created. There may be an
    //   SD card problem. Just return.
    //
    // - Otherwise write the settings to the file.
    SdFile file;
    file.open( USAGE_FILENAME, O_WRONLY|O_CREAT );
    if ( !file )
        return false;

    // See comments about SdFat's write() and sync() in the body of the
    // writeDataLogHeader() method above.
    for ( int i = 0; i < 8; ++i )
    {
        switch ( i )
        {
            case 0:
                sprintf( sharedBuffer, "numberOfBoots %ld\r\n",
                    usage.numberOfBoots );
                break;
            case 1:
                sprintf( sharedBuffer, "numberOfCameraBoots %ld\r\n",
                    usage.numberOfCameraBoots );
                break;
            case 2:
                sprintf( sharedBuffer, "numberOfLaserBoots %ld\r\n",
                    usage.numberOfLaserBoots );
                break;
            case 3:
                sprintf( sharedBuffer, "numberOfEventsLogged %ld\r\n",
                    usage.numberOfEventsLogged );
                break;
            case 4:
                sprintf( sharedBuffer, "numberOfImagesSnapped %ld\r\n",
                    usage.numberOfImagesSnapped );
                break;
            case 5:
                sprintf( sharedBuffer, "controllerUptimeSeconds %ld\r\n",
                    usage.controllerUptimeSeconds );
                break;
            case 6:
                sprintf( sharedBuffer, "cameraUptimeSeconds %ld\r\n",
                    usage.cameraUptimeSeconds );
                break;
            case 7:
                sprintf( sharedBuffer, "laserUptimeSeconds %ld\r\n",
                    usage.laserUptimeSeconds );
                break;
        }

        const uint32_t nBytes = strlen( sharedBuffer );
        if ( file.write( sharedBuffer, nBytes ) != nBytes ||
             !file.sync( ) )
        {
            cardErrorCode  = sd.sdErrorCode( );  // Probably NONE.
            localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
            file.close( );
            return false;
        }
    }

    file.close( );
    return true;
}
#endif





//----------------------------------------------------------------------
// Error log file.
//----------------------------------------------------------------------
/**
 * Appends a message to the status log, creating the file if needed.
 *
 * @param[in] message
 *   The message to append, along with a timestamp.
 *
 * @return
 *   Returns true on success. On failure, false is returned and error codes
 *   are set. Possible failures:
 *   - The SD card is not inserted.
 *   - The SD card is full.
 *   - The file has reached the 4GB max size for FAT.
 *   - A hardware error has occurred.
 *   - An internal SdFat error has occurred.
 */
bool FileSystem::writeStatus( const char*const message )
{
    if ( !initialized )
        return false;

    SdFile file;
    bool status    = true;
    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;

    // Create or overwrite status file.
    //
    // See comments about SdFat's write() and sync() in the body of the
    // writeDataLogHeader() method above.
    const bool alreadyExisted = sd.exists( STATUS_LOG_FILENAME );
    file.open( STATUS_LOG_FILENAME, O_WRONLY|O_CREAT|O_APPEND );
    if ( !file )
    {
        // File open/create failed. Rely upon SdFat's error codes.
        cardErrorCode = sd.sdErrorCode( );
        status = false;
    }
    else
    {
        const char* now = Clock::nowString( );
        if ( !alreadyExisted )
        {
            // The file has just been created. Add a first message.
            sprintf( sharedBuffer, "%s\tLog file created\r\n", now );
            const uint32_t nBytes = strlen( sharedBuffer );
            if ( file.write( sharedBuffer, nBytes ) != nBytes )
            {
                // File write failed. The SD card may be full.
                cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
                if ( sd.card( )->sectorCount( ) <= 0 )
                    localErrorCode = FS_ERROR_NOCARD;
                else
                    localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
                status = false;
            }
        }
        if ( *message == '\0' )
        {
            if ( file.write( "\r\n\r\n", 4 ) != 4 )
            {
                // File write failed. The SD card may be full.
                cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
                if ( sd.card( )->sectorCount( ) <= 0 )
                    localErrorCode = FS_ERROR_NOCARD;
                else
                    localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
                status = false;
            }
        }
        else
        {
            sprintf( sharedBuffer, "%s\t%s\r\n", now, message );
            const uint32_t nBytes = strlen( sharedBuffer );
            if ( file.write( sharedBuffer, nBytes ) != nBytes )
            {
                // File write failed. The SD card may be full.
                cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
                if ( sd.card( )->sectorCount( ) <= 0 )
                    localErrorCode = FS_ERROR_NOCARD;
                else
                    localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
                status = false;
            }
        }

        if ( !file.sync( ) )
        {
            // File sync failed. The SD card may be full.
            cardErrorCode = sd.sdErrorCode( );  // Probably NONE.
            if ( sd.card( )->sectorCount( ) <= 0 )
                localErrorCode = FS_ERROR_NOCARD;
            else
                localErrorCode = FS_ERROR_CARD_FULL; // Best guess.
            status = false;
        }
        file.close( );
    }

    return status;
}





//----------------------------------------------------------------------
// POSIX-style operations.
//----------------------------------------------------------------------
/**
 * Shows a file's content on the serial port.
 *
 * Problems are printed to the serial port.
 *
 * @param[in] path
 *   The path of a file or directory.
 *
 * @return
 *   Returns true on sucess or recoverable problems, and false on
 *   I/O errors.
 */
bool FileSystem::cat( const char*const path )
{
    bool status    = true;
    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;

    SdFile file( path, O_RDONLY );
    if ( !file.isOpen( ) )
    {
        localErrorCode = FS_ERROR_BAD_PATH;
        status = false;
    }
    else if ( file.isDir( ) )
    {
        localErrorCode = FS_ERROR_IS_DIR;
        status = false;
    }
    else
    {
        int16_t nBytes;
        while ( (nBytes = file.read( sharedBuffer, BUFFER_SIZE-1 )) > 0 )
        {
            sharedBuffer[nBytes] = '\0';
            Serial.print( sharedBuffer );
        }
        if ( nBytes > 0 && sharedBuffer[nBytes-1] != '\n' )
            Serial.printf( "\r\n" );

        cardErrorCode = sd.sdErrorCode( );
        if ( cardErrorCode != SD_CARD_ERROR_NONE )
            status = false;
    }
    file.close( );

    return status;
}





/**
 * Gets the storage space used by the directory and its contents.
 *
 * @param[in] path
 *   The path of a file or directory.
 * @param[in] isTop
 *   (optional, default = true) True for the top-most call, and false
 *   during recursion.
 *
 * @return
 *   Returns the size, in bytes, on success. On failure, returns zero
 *   and error codes are set.
 */
uint64_t FileSystem::du( const char*const path, const bool isTop )
{
    if ( isTop )
    {
        if ( !isCardPresent( ) )
        {
            cardErrorCode = sd.sdErrorCode( );
            localErrorCode = FS_ERROR_NOCARD;
            return 0;
        }
        localErrorCode = FS_ERROR_NONE;
        cardErrorCode  = SD_CARD_ERROR_NONE;
    }

    uint64_t nBytes = 0;
    SdFile fileOrDir( path, O_RDONLY );
    if ( !fileOrDir.isOpen( ) )
        localErrorCode = FS_ERROR_BAD_PATH;
    else if ( !fileOrDir.isDir( ) )
        nBytes = fileOrDir.fileSize( );
    else
    {
        nBytes = fileOrDir.fileSize( );

        // Recurse through directory's children.
        SdFile entry;
        while ( entry.openNext( &fileOrDir, O_RDONLY ) )
        {
            entry.getName( sharedFilename, MAX_FILENAME+1 );
            const String s = String( path ) + "/" + sharedFilename;
            entry.close( );
            nBytes += du( s.c_str( ), false );
        }
    }
    fileOrDir.close( );

    return nBytes;
}





/**
 * Shows first 10 lines of file's content on the serial port.
 *
 * Problems are printed to the serial port.
 *
 * @param[in] path
 *   The path of a file or directory.
 *
 * @return
 *   Returns true on sucess or recoverable problems, and false on
 *   I/O errors.
 */
bool FileSystem::head( const char*const path )
{
    if ( !isCardPresent( ) )
    {
        cardErrorCode = sd.sdErrorCode( );
        localErrorCode = FS_ERROR_NOCARD;
        return false;
    }

    bool status = true;
    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;

    SdFile file( path, O_RDONLY );
    if ( !file.isOpen( ) )
    {
        localErrorCode = FS_ERROR_BAD_PATH;
        status = false;
    }
    else if ( file.isDir( ) )
    {
        localErrorCode = FS_ERROR_IS_DIR;
        status = false;
    }
    else
    {
        uint32_t nLines = 0;
        int16_t nBytes  = 0;

        while ( nLines < HEAD_LINES &&
            (nBytes = file.read( sharedBuffer, BUFFER_SIZE-1 )) > 0 )
        {
            sharedBuffer[nBytes] = '\0';

            // Count line ends in the buffer.
            for ( uint16_t i = 0; i < nBytes; ++i )
            {
                if ( sharedBuffer[i] == '\n' )
                {
                    ++nLines;
                    if ( nLines >= HEAD_LINES )
                    {
                        sharedBuffer[i+1] = '\0';
                        break;
                    }
                }
            }

            Serial.print( sharedBuffer );
        }
        if ( nBytes > 0 && sharedBuffer[nBytes-1] != '\n' )
            Serial.printf( "\r\n" );

        cardErrorCode = sd.sdErrorCode( );
        if ( cardErrorCode != SD_CARD_ERROR_NONE )
            status = false;
    }
    file.close( );

    return status;
}





/**
 * Lists a file or directory to the serial port.
 *
 * Problems are printed to the serial port.
 *
 * @param[in] path
 *   The path to list.
 *
 * @return
 *   Returns true on sucess or recoverable problems, and false on
 *   I/O errors.
 */
bool FileSystem::ls( const char*const path )
{
    if ( !isCardPresent( ) )
    {
        cardErrorCode = sd.sdErrorCode( );
        localErrorCode = FS_ERROR_NOCARD;
        return false;
    }

    bool status = true;
    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;
    SdFile fileOrDir( path, O_RDONLY );
    if ( !fileOrDir.isOpen( ) )
    {
        localErrorCode = FS_ERROR_BAD_PATH;
        status = false;
    }
    else
    {
        if ( !fileOrDir.isDir( ) )
        {
            // The item is a file. Print its name and size.
            fileOrDir.getName( sharedFilename, MAX_FILENAME+1 );
            const uint32_t size = fileOrDir.fileSize( );
            Serial.printf( "%-20s %9ld\r\n", sharedFilename, size );
        }
        else
        {
            // The item is a directory. List contents.
            SdFile entry;
            while ( entry.openNext( &fileOrDir, O_RDONLY ) )
            {
                entry.getName( sharedFilename, MAX_FILENAME+1 );
                if (entry.isDir( ) )
                    Serial.printf( "%s/\r\n", sharedFilename );
                else
                {
                    const uint32_t size = entry.fileSize( );
                    Serial.printf( "%-20s %9ld\r\n", sharedFilename, size );
                }
                entry.close( );
            }
        }
        fileOrDir.close( );
    }

    return status;
}






/**
 * Removes a file or a non-empty directory recursively.
 *
 * @param[in] path
 *   The path of a file or directory.
 * @param[in] isTop
 *   (optional, default = true) True for the top-most call, and false
 *   during recursion.
 *
 * @return
 *   Returns true on sucess or recoverable problems, and false on
 *   I/O errors.
 */
bool FileSystem::rmall( const char*const path, const bool isTop )
{
    if ( isTop )
    {
        if ( !isCardPresent( ) )
        {
            cardErrorCode = sd.sdErrorCode( );
            localErrorCode = FS_ERROR_NOCARD;
            return false;
        }
        localErrorCode = FS_ERROR_NONE;
        cardErrorCode  = SD_CARD_ERROR_NONE;
    }

    bool status = true;
    SdFile fileOrDir( path, O_RDONLY );
    if ( !fileOrDir.isOpen( ) )
    {
        localErrorCode = FS_ERROR_BAD_PATH;
        status = false;
    }
    else if ( !fileOrDir.isDir( ) )
    {
        fileOrDir.close( );
        if ( !sd.remove( path ) )
        {
            if ( hasError( ) )
                Serial.printf( "Error: %s\r\n", getErrorMessage( ) );
            else
                localErrorCode = FS_ERROR_CANNOT_RM;
            status = false;
        }
    }
    else
    {
        // The item is a directory. Recurse through its children.
        const bool isRoot = (strcmp( path, "/" ) == 0);
        SdFile entry;
        while ( entry.openNext( &fileOrDir, O_RDONLY ) )
        {
            entry.getName( sharedFilename, MAX_FILENAME+1 );
            const String s = isRoot ?
                (String( "/" ) + sharedFilename) :
                (String( path ) + "/" + sharedFilename);
            entry.close( );

            if ( !rmall( s.c_str( ), false ) )
            {
                status = false;
                break;
            }
        }
        fileOrDir.close( );

        if ( status && !isRoot )
        {
            // If the directory is NOT root, then delete it.
            if ( !sd.rmdir( path ) )
            {
                if ( hasError( ) )
                    Serial.printf( "Error: %s\r\n", getErrorMessage( ) );
                else
                    localErrorCode = FS_ERROR_CANNOT_RM;
                status = false;
            }
        }
    }

    return status;
}





/**
 * Shows last 10 lines of file's content on the serial port.
 *
 * Problems are printed to the serial port.
 *
 * @param[in] path
 *   The path of a file or directory.
 *
 * @return
 *   Returns true on sucess or recoverable problems, and false on
 *   I/O errors.
 */
bool FileSystem::tail( const char*const path )
{
    if ( !isCardPresent( ) )
    {
        cardErrorCode = sd.sdErrorCode( );
        localErrorCode = FS_ERROR_NOCARD;
        return false;
    }

    bool status = true;
    localErrorCode = FS_ERROR_NONE;
    cardErrorCode  = SD_CARD_ERROR_NONE;
    SdFile file( path, O_RDONLY );
    if ( !file.isOpen( ) )
    {
        localErrorCode = FS_ERROR_BAD_PATH;
        status = false;
    }
    else if ( file.isDir( ) )
    {
        localErrorCode = FS_ERROR_IS_DIR;
        status = false;
    }
    else
    {
        uint32_t nLines = 0;
        int32_t nBytes = 0;

        // Move to the end of the file, then back one buffer's worth.
        uint64_t nToRead = BUFFER_SIZE - 1;
        int64_t offset = file.fileSize( ) - nToRead;
        if ( offset < 0 )
        {
            // Too far. Reset to start of file.
            nToRead = offset + BUFFER_SIZE - 1;
            offset = 0;
        }
        file.seekSet( offset );

        // Scan backwards, a buffer at a time, counting end of lines
        // until we find the end of the line before the first line
        // we want to print.
        while ( nLines <= TAIL_LINES &&
            (nBytes = file.read( sharedBuffer, nToRead )) > 0 )
        {
            // Count line ends in the buffer.
            for ( int32_t i = nBytes-1; i >= 0; --i )
            {
                if ( sharedBuffer[i] == '\n' )
                {
                    ++nLines;
                    if ( nLines > TAIL_LINES )
                    {
                        // Move the offset to the last end of line,
                        // plus one to skip over the end of line.
                        // Then end the read loop.
                        offset += i + 1;
                        break;
                    }
                }
            }

            if ( nLines > TAIL_LINES || offset == 0 )
                break;

            // Back up another buffer's worth.
            offset -= BUFFER_SIZE;
            if ( offset < 0 )
            {
                // Too far. Reset to start of file.
                nToRead = offset + BUFFER_SIZE - 1;
                offset = 0;
            }
            file.seekSet( offset );
        }

        // Move the file offset to the start of the first tail line,
        // then read and print everything from there to the end of the file.
        file.seekSet( offset );
        while ( (nBytes = file.read( sharedBuffer, BUFFER_SIZE-1 )) > 0 )
        {
            sharedBuffer[nBytes] = '\0';
            Serial.print( sharedBuffer );
        }
        if ( nBytes > 0 && sharedBuffer[nBytes-1] != '\n' )
            Serial.printf( "\r\n" );

        cardErrorCode = sd.sdErrorCode( );
        if ( cardErrorCode != SD_CARD_ERROR_NONE )
            status = false;
    }
    file.close( );

    return status;
}
