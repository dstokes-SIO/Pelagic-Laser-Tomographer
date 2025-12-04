#pragma once
#include <Arduino.h>
#include <SdFat.h>

#include "pltlogger.h"

// Define to include battery columns in the data log.
#define BATTERY_IN_DATA_LOG

#define FS_ERROR_CODE_LIST \
    FS_ERROR(NONE, "No error.")\
    FS_ERROR(UNINITIALIZED, "Initialization failure.")\
    FS_ERROR(NOCARD, "Missing SD card or bad card format.")\
    FS_ERROR(BAD_FORMAT, "Unsupported SD card format.")\
    FS_ERROR(CARD_FULL, "SD card is full.")\
    FS_ERROR(TOO_MANY_LOG_FILES, "Too many log files; 100 max.")\
    FS_ERROR(BAD_PATH, "No such file or directory.")\
    FS_ERROR(IS_DIR, "Path is for a directory, not a file.")\
    FS_ERROR(IS_FILE, "Path is for a file, not a directory.")\
    FS_ERROR(CANNOT_RM, "Cannot remove file or directory.")


/**
 * Manages file system activity.
 *
 * This class handles:
 *
 * - Initializing access to the SD card.
 * - Formating the SD card.
 * - POSIX-style cat, head, tail, du, rm, and ls.
 * - Open, write, and close for a CSV log file.
 * - Open, write, and close for a status log file.
 * - Open, read, write, and close for a settings file.
 * - Open, read, write, and close for a stats file.
 *
 * The CSV log file records a timestamp and sensor readings with one row
 * per camera imaging event. Along with separate camera images, this is
 * the primary data for the device's use.
 *
 * The status log file records a timestamp and message with one row per
 * major device event, such as the device boot, recording start/stop, and
 * errors.
 *
 * The settings file records values for configuration parameters that need
 * to persist from one boot to the next, including the frame interval.
 * The file is read at device boot and used to initialize parameters. It is
 * also written each time one of the parameters is changed.
 *
 * The stats file records counters that record long term usage, such as
 * the number of images captured, the number of power cycles, and the
 * total run time. The file is read at device boot to initialize counters,
 * and written periodically to save values so that the persist to the next
 * boot.
 */
class FileSystem
{
private:
    FileSystem( ) = delete;
    FileSystem( const FileSystem& ) = delete;
    FileSystem& operator=( const FileSystem& ) = delete;


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
public:
    // Local error codes.
    enum
    {
#define FS_ERROR(e, m) FS_ERROR_##e,
        FS_ERROR_CODE_LIST
#undef FS_ERROR
    };

private:
    // Head, tail, and cat buffer size.
    static const uint32_t BUFFER_SIZE = 1025;

    // Maximum FAT file name. The maximum name size varies with the file
    // system type:
    //   FAT16 and FAT32: 12 (8.3 names)
    //   ExFAT: 255
    static const uint16_t MAX_FILENAME = 255;

    // Log file name format.
    static const char*const DATA_LOG_FILENAME_FORMAT;

public:
    // Head and tail limits.
    static const uint32_t HEAD_LINES = 10;
    static const uint32_t TAIL_LINES = 10;

    // Maximum number of log files. While FAT32 will allow up to 65k files
    // in the same directory, performance becomes very very poor. Since
    // the number of data log files needed is limited by this performance
    // and by the practicality of the device's use to capture images along
    // with log file entries, the maximum number is intentionally low.
    static const uint16_t MAX_LOG_FILES = 100;

    // Settings file name.
    static const char*const SETTINGS_FILENAME;

#if defined(ENABLE_USAGE_TRACKING)
    // Usage tracking file name.
    static const char*const USAGE_FILENAME;
#endif

    // Error log file name.
    static const char*const STATUS_LOG_FILENAME;


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    // Primary file system access.
    static SdFat sd;

    // The current log file, if any.
    static SdFile logFile;

    // Shared filename holder. To avoid using stack space or multiple
    // static arrays, all methods that need a place to put a filename
    // share a single array.
    static char sharedFilename[MAX_FILENAME+1];

    // Shared file I/O buffer. To avoid using stack space for a large
    // temporary buffer, all methods that need a read/write buffer share
    // a single buffer.
    static char sharedBuffer[BUFFER_SIZE];

    // Initialization and recent error codes.
    static bool initialized;
    static uint8_t cardErrorCode;
    static uint8_t localErrorCode;

    // The number of entries written to the current log file.
    static uint32_t numberOfDataLogEntries;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
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
    static bool init( );

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
    static bool format( );

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
    static const char* getErrorMessage( );

    /**
     * Prints the most recent error message.
     */
    static inline void printErrorMessage( )
    {
        Serial.printf( "%s\r\n", getErrorMessage( ) );
    }

    /**
     * Returns the most recent file system error code.
     *
     * @return
     *   The error code.
     */
    static inline uint8_t getErrorCode( )
    {
        return localErrorCode;
    }

    /**
     * Returns the most recent SD card error code.
     *
     * @return
     *   The error code.
     */
    static inline uint8_t getSdCardErrorCode( )
    {
        return cardErrorCode;
    }

    /**
     * Returns true if there is an error pending.
     *
     * @return
     *   Returns true on error.
     *
     * @see getErrorMessage()
     */
    static inline bool hasError( )
    {
        return (localErrorCode != FS_ERROR_NONE) ||
            (cardErrorCode != SD_CARD_ERROR_NONE);
    }

    /**
     * Returns true if initialized.
     *
     * @return
     *   Returns true if initialized.
     *
     * @see init()
     * @see format()
     */
    static inline bool isInitialized( )
    {
        return initialized;
    }

    /**
     * Returns true if there is an SD card present.
     *
     * If an SD card is removed after device boot, the SdFat library may
     * still allow some file operations (like "ls") because they work on
     * cached information. The success/fail status of such operations, then,
     * is not indicative of an SD card being present. This method may be used
     * to be somewhat more definitive.
     *
     * @return
     *   Returns true if a card is present.
     */
    static inline bool isCardPresent( )
    {
        if ( !initialized )
            return false;
        if ( sd.card( )->sectorCount( ) > 0 )
            return true;
        localErrorCode = FS_ERROR_NOCARD;
        return false;
    }


//----------------------------------------------------------------------
// Utilities.
//----------------------------------------------------------------------
private:
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
    static void parseLine(
        char*const string,
        char*& name,
        char*& value );

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
    static int16_t readLine( SdFile& file );


//----------------------------------------------------------------------
// Attributes.
//----------------------------------------------------------------------
public:
    /**
     * Returns the SD card FAT format type.
     *
     * @return
     *   Returns the FAT type as one of: 16 or 32.
     */
    static inline uint8_t getFatType( )
    {
        return sd.vol( )->fatType( );
    }

    /**
     * Returns the SD card capacity, in bytes.
     *
     * If there is no SD card present, zero is returned.
     *
     * @return
     *   Returns the card capacity.
     */
    static inline uint64_t getCardCapacity( )
    {
        return (uint64_t)sd.card( )->sectorCount( ) * 512L;
    }

    /**
     * Returns the storage space in use by the SD card, as a percent.
     *
     * If there is no SD card present, zero is returned.
     *
     * @return
     *   Returns the percent of the card's storage in use.
     */
    static inline float getSpaceUsedPercent( )
    {
        const uint64_t capacity = getCardCapacity( );
        if ( capacity == 0 )
            return 0.0;
        const uint64_t inUse = getSpaceUsed( );
        return 100.0 * (double)(inUse >> 8) / (double)(capacity >> 8);
    }

    /**
     * Returns the storage space in use by the SD card, in bytes.
     *
     * This may take awhile.
     *
     * If there is no SD card present, zero is returned.
     *
     * @return
     *   Returns the size, in bytes.
     */
    static inline uint64_t getSpaceUsed( )
    {
#ifdef USE_FREE_SPACE
        // The freeClusterCount() call is very slow in stock SdFat.
        return (uint64_t)sd.bytesPerCluster( ) *
            ((uint64_t)sd.clusterCount( ) - (uint64_t)sd.freeClusterCount( ));
#else
        if ( sd.card( )->sectorCount( ) == 0 )
            return 0;

        // Since we expect a fairly small number of files on the SD card,
        // it is probably much faster to count up their sizes than to
        // count free clusters. This is NOT as accurate because it does not
        // count format overhead.
        return du( "/" );
#endif
    }


//----------------------------------------------------------------------
// Data log file.
//----------------------------------------------------------------------
public:
    /**
     * Closes the current log file, if any.
     *
     * @see getDataLogFilename()
     * @see getNumberOfDataLogEntries()
     * @see isDataLogOpen()
     * @see newDataLog()
     * @see writeDataLog()
     * @see writeDataLogHeader()
     */
    static void closeDataLog( )
    {
        // If there is no log file, this does nothing.
        logFile.close( );
        numberOfDataLogEntries = 0;
        localErrorCode = FS_ERROR_NONE;
    }

    /**
     * Creates a new unique log file.
     *
     * If there is a previous log file, it is closed.
     *
     * @return
     *   Returns true on success and false on failure.
     *
     * @see closeDataLog()
     * @see getDataLogFilename()
     * @see getNumberOfDataLogEntries()
     * @see isDataLogOpen()
     * @see writeDataLog()
     * @see writeDataLogHeader()
     */
    static bool newDataLog( );

    /**
     * Returns the name of the currently open log file.
     *
     * If there is no log file open, an empty string is returned.
     *
     * All calls to this method, and many other methods, share a single
     * file name array that is overwritten with each call.
     *
     * @return
     *   Returns the log file name.
     *
     * @see closeDataLog()
     * @see getNumberOfDataLogEntries()
     * @see isDataLogOpen()
     * @see newDataLog()
     * @see writeDataLog()
     * @see writeDataLogHeader()
     */
    static inline const char* getDataLogFilename( )
    {
        sharedFilename[0] = '\0';
        logFile.getName( sharedFilename, MAX_FILENAME+1 );
        return sharedFilename;
    }

    /**
     * Returns the number of entries in the current log.
     *
     * If there is no log file open, zero is returned.
     *
     * @return
     *   Returns the number of entries.
     *
     * @see closeDataLog()
     * @see getDataLogFilename()
     * @see isDataLogOpen()
     * @see newDataLog()
     * @see writeDataLog()
     * @see writeDataLogHeader()
     */
    static inline uint32_t getNumberOfDataLogEntries( )
    {
        return numberOfDataLogEntries;
    }

    /**
     * Returns true if there is a log file open.
     *
     * @return
     *   Returns true if open.
     *
     * @see closeDataLog()
     * @see getDataLogFilename()
     * @see getNumberOfDataLogEntries()
     * @see newDataLog()
     * @see writeDataLog()
     * @see writeDataLogHeader()
     */
    static inline bool isDataLogOpen( )
    {
        if ( logFile )
            return true;
        return false;
    }

private:
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
     *   Returns false if there is no log file open or an error occurred.
     *
     * @see closeDataLog()
     * @see getErrorMessage()
     * @see getDataLogFilename()
     * @see getNumberOfDataLogEntries()
     * @see hasError()
     * @see isDataLogOpen()
     * @see newDataLog()
     * @see writeDataLog()
     */
    static bool writeDataLogHeader( );

public:
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
     *   Returns false if there is no log file open or an error occurred.
     *
     * @see closeDataLog()
     * @see getDataLogFilename()
     * @see getNumberOfDataLogEntries()
     * @see isDataLogOpen()
     * @see newDataLog()
     * @see writeDataLogHeader()
     */
    static bool writeDataLog(
        const char*const dt,
        const uint32_t ms,
        const float pressure,
        const float depth,
        const float waterTemperature,
        const float deviceTemperature,
        const float* accel,
        const float* mag,
        const float* gyro );


//----------------------------------------------------------------------
// Settings file.
//----------------------------------------------------------------------
public:
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
    static bool loadSettings(
        uint32_t &interval,
        bool &isLaserContinuous,
        uint8_t &burstSize );

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
    static bool saveSettings(
        const uint32_t interval,
        const bool isLaserContinuous,
        const uint8_t burstSize );


#if defined(ENABLE_USAGE_TRACKING)
//----------------------------------------------------------------------
// Stats file.
//----------------------------------------------------------------------
public:
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
    static bool loadUsage( Usage& usage );

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
    static bool saveUsage( const Usage& usage );
#endif


//----------------------------------------------------------------------
// Error log file.
//----------------------------------------------------------------------
public:
    /**
     * Appends a message to the activity log, creating the file if needed.
     *
     * @param[in] message
     *   The message to append, along with a timestamp.
     *
     * @return
     *   Returns true on sucess.
     */
    static bool writeStatus( const char*const message );


//----------------------------------------------------------------------
// POSIX-style operations.
//----------------------------------------------------------------------
public:
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
    static bool cat( const char*const path );

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
     *   Returns the size.
     */
    static uint64_t du( const char*const path, const bool isTop = true );

    /**
     * Shows first 10 lines of file's content on the serial port.
     *
     * Problems are printed to the serial port.
     *
     * @param[in] path
     *   The path of a file or directory.
     * @return
     *   Returns true on sucess or recoverable problems, and false on
     *   I/O errors.
     */
    static bool head( const char*const path );

    /**
     * Lists a file or directory to the serial port.
     *
     * @param[in] path
     *   The path to list.
     *
     * @return
     *   Returns true on sucess or recoverable problems, and false on
     *   I/O errors.
     */
    static bool ls( const char*const path );

    /**
     * Removes a file.
     *
     * @param[in] path
     *   The path of a file (not a directory).
     *
     * @return
     *   Returns true on sucess or recoverable problems, and false on
     *   I/O errors.
     */
    static bool rm( const char*const path );

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
    static bool rmall( const char*const path, const bool isTop = true );

    /**
     * Removes an empty directory.
     *
     * @param[in] path
     *   The path of a directory (not a file).
     * @return
     *   Returns true on success, false on failure. On failure, an error
     *   message is output to the serial port.
     */
    static bool rmdir( const char*const path );

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
    static bool tail( const char*const path );
};
