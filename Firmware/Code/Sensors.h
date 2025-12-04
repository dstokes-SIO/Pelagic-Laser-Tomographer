#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <Adafruit_LSM9DS1.h>   // Gyroscope, Accelerometer, & Magnetometer
#include <MS5837.h>             // Pressure, depth, and temperature sensor
#include <TSYS01.h>             // Temperature sensor

#include "pltlogger.h"


/**
 * Manages device sensors.
 *
 * Sensors include:
 *
 * - An LSM9DS1 9-axis iNEMO inertial module (IMU). The sensor incorporates
 *   a 3D magnetometer, 3D accelerometer, and 3D gyroscope. The module also
 *   reports the internal device temperature.
 *
 * - A MS5837 30 bar digital pressure sensor. The sensor reports water
 *   pressure and depth to a 2mm resolution. An included low-resolution
 *   water temperature sensor is not used.
 *
 * - A TSYS01 digital temperature sensor. The high-resolution sensor reports
 *   water temperature between -5 and 50 C, +/- 0.1C.
 *
 * @see https://www.st.com/en/mems-and-sensors/lsm9ds1.html
 * @see https://www.st.com/resource/en/datasheet/lsm9ds1.pdf
 * @see https://www.mouser.com/new/measurement-specialties/te-ms5837-30ba/
 * @see https://www.te.com/usa-en/product-CAT-BLPS0017.html
 * @see https://www.te.com/commerce/DocumentDelivery/DDEController?Action=showdoc&DocId=Data+Sheet%7FTSYS01%7FA%7Fpdf%7FEnglish%7FENG_DS_TSYS01_A.pdf%7FG-NICO-018
 * @see https://www.mouser.com/ProductDetail/Measurement-Specialties/TSYS01/?qs=X3dlXJsz9DAvF3tElFUMhA%3D%3D
 */
class Sensors
{
private:
    Sensors( ) = delete;
    Sensors( const Sensors& ) = delete;
    Sensors& operator=( const Sensors& ) = delete;


//----------------------------------------------------------------------
// Constants.
//----------------------------------------------------------------------
public:
    // Water density for fresh water (997 kg/m^3 for fresh water).
    static constexpr float FRESHWATER = 997.0;

    // Water density for sea water (1029 kg/m^3 for sea water).
    static constexpr float SALTWATER = 1029.0;

    // Celsius -274 = 0 Kelvin = lowest possible temperature.
    static constexpr float BAD_WATER_TEMPERATURE = -274.0;

    // Initialization state.
    static const uint8_t INERTIA_INITIALIZED     = 0x01;
    static const uint8_t PRESSURE_INITIALIZED    = 0x02;
    static const uint8_t TEMPERATURE_INITIALIZED = 0x04;
    static const uint8_t ALL_INITIALIZED =
        (INERTIA_INITIALIZED | PRESSURE_INITIALIZED | TEMPERATURE_INITIALIZED);


//----------------------------------------------------------------------
// Fields.
//----------------------------------------------------------------------
private:
    static Adafruit_LSM9DS1 inertiaSensor;
    static MS5837 pressureSensor;
    static TSYS01 temperatureSensor;
    static uint8_t initialized;


//----------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------
public:
    /**
     * Initializes the sensors.
     *
     * @param[in] waterDensity
     *   (optional, default = Sensors::SALTWATER) The water density for
     *   the pressure sensor, in kg/m^3. Usually one of: Sensors::FRESHWATER
     *   or Sensors::SALTWATER.
     *
     * @return
     *   Returns true on success and false on failure.
     *
     * @see isInitialized()
     * @see isInertiaSensorPresent()
     * @see isPressureSensorPresent()
     * @see isTemperatureSensorPresent()
     * @see FRESHWATER
     * @see SALTWATER
     */
    static inline bool init( const float waterDensity = SALTWATER )
    {
        initialized = 0;

        // Inertia sensor. Initialize the library and report failure.
        // Use the default pin assignments.
        if ( inertiaSensor.begin( ) )
        {
            // Use a 2 gauss range for the accelerometer.
            inertiaSensor.setupAccel( inertiaSensor.LSM9DS1_ACCELRANGE_2G );

            // Use a 4 gauss range for the magnetometer.
            inertiaSensor.setupMag( inertiaSensor.LSM9DS1_MAGGAIN_4GAUSS );

            // Use a 245 degrees/second range for the gyroscope.
            inertiaSensor.setupGyro( inertiaSensor.LSM9DS1_GYROSCALE_245DPS );

            initialized |= INERTIA_INITIALIZED;
        }
#if defined(DEBUG_VERBOSE_SENSORS)
        if ( (initialized & INERTIA_INITIALIZED) != 0 )
            Serial.print( "Debug: Inertia sensor initialized.\r\n" );
        else
            Serial.print( "Debug: Inertia sensor initialization FAIL.\r\n" );
#endif


        // Pressure sensor. Initialize the library and report failure.
        if ( pressureSensor.init( ) )
        {
            // Set pressure sensor to the 30-bar model (the default).
            pressureSensor.setModel( MS5837::MS5837_30BA );

            // Set pressure sensor fluid density.
            pressureSensor.setFluidDensity( waterDensity );

            initialized |= PRESSURE_INITIALIZED;
        }
#if defined(DEBUG_VERBOSE_SENSORS)
        if ( (initialized & PRESSURE_INITIALIZED) != 0 )
            Serial.print( "Debug: Pressure sensor initialized.\r\n" );
        else
            Serial.print( "Debug: Pressure sensor initialization FAIL.\r\n" );
#endif


        // Temperature sensor. The library's init() does not return
        // anything. But if a temperature reading is ridiculous, fail.
        temperatureSensor.init( );
        temperatureSensor.read( );
        const float temp = temperatureSensor.temperature( );
        if ( temp > BAD_WATER_TEMPERATURE )
            initialized |= TEMPERATURE_INITIALIZED;
#if defined(DEBUG_VERBOSE_SENSORS)
        if ( (initialized & TEMPERATURE_INITIALIZED) != 0 )
            Serial.print( "Debug: Temperature sensor initialized.\r\n" );
        else
            Serial.print( "Debug: Temperature sensor initialization FAIL.\r\n" );
#endif

        // Return true only if all sensors initialized.
        return (initialized == ALL_INITIALIZED);
    }

    /**
     * Checks if all sensors are initialized.
     *
     * @return
     *   Returns true if initialized.
     *
     * @see init()
     * @see isInertiaSensorPresent()
     * @see isPressureSensorPresent()
     * @see isTemperatureSensorPresent()
     */
    static inline bool isInitialized( )
    {
        return initialized == ALL_INITIALIZED;
    }

    /**
     * Checks if the inertia sensor is initialized.
     *
     * @return
     *   Returns true if initialized.
     *
     * @see init()
     * @see getInertiaSensorName()
     */
    static inline bool isInertiaSensorPresent( )
    {
        return (initialized & INERTIA_INITIALIZED) != 0;
    }

    /**
     * Checks if the pressure sensor is initialized.
     *
     * @return
     *   Returns true if initialized.
     *
     * @see init()
     * @see getPressureSensorName()
     */
    static inline bool isPressureSensorPresent( )
    {
        return (initialized & PRESSURE_INITIALIZED) != 0;
    }

    /**
     * Checks if the temperature sensor is initialized.
     *
     * @return
     *   Returns true if initialized.
     *
     * @see init()
     * @see getTemperatureSensorName()
     */
    static inline bool isTemperatureSensorPresent( )
    {
        return (initialized & TEMPERATURE_INITIALIZED) != 0;
    }

    /**
     * Returns the name of the inertia sensor.
     *
     * @return
     *   Returns the name.
     *
     * @see init()
     * @see isInertiaSensorPresent()
     */
    static inline const char* getInertiaSensorName( )
    {
        return "LSM9DS1 inertia module";
    }

    /**
     * Returns the name of the pressure sensor.
     *
     * @return
     *   Returns the name.
     *
     * @see init()
     * @see isPressureSensorPresent()
     */
    static inline const char* getPressureSensorName( )
    {
        return "MS5837 pressure sensor";
    }

    /**
     * Returns the name of the temperature sensor.
     *
     * @return
     *   Returns the name.
     *
     * @see init()
     * @see isTemperatureSensorPresent()
     */
    static inline const char* getTemperatureSensorName( )
    {
        return "TSYS01 temperature sensor";
    }



//----------------------------------------------------------------------
// Methods.
//----------------------------------------------------------------------
public:
    /**
     * Returns the current inertia sensor values.
     *
     * @param[out] accel
     *   A 3-element array returning linear acceleration values for X,
     *   Y, and Z. Values are in gauss.
     * @param[out] mag
     *   A 3-element array returning linear acceleration values for X,
     *   Y, and Z. Values are in gauss.
     * @param[out] gyro
     *   A 3-element array returning linear acceleration values for X,
     *   Y, and Z. Values are in degrees/second.
     * @param[out] temp
     *   The returned device temperature, in Celsius.
     *
     * @see isInertiaSensorPresent()
     * @see https://www.arduino.cc/en/Reference/ArduinoLSM9DS1
     */
    static inline void getInertia(
        float* accel,
        float* mag,
        float* gyro,
        float& temp )
    {
        if ( !isInertiaSensorPresent( ) )
        {
            // No sensor. Return zeroes.
            accel[0] = 0.0;
            accel[1] = 0.0;
            accel[2] = 0.0;
            mag[0] = 0.0;
            mag[1] = 0.0;
            mag[2] = 0.0;
            gyro[0] = 0.0;
            gyro[1] = 0.0;
            gyro[2] = 0.0;
            return;
        }

        // Read the sensor.
        sensors_event_t a, m, g, t;
        inertiaSensor.getEvent( &a, &m, &g, &t );

        accel[0] = a.acceleration.x;
        accel[1] = a.acceleration.y;
        accel[2] = a.acceleration.z;

        mag[0] = m.magnetic.x;
        mag[1] = m.magnetic.y;
        mag[2] = m.magnetic.z;

        gyro[0] = g.gyro.x;
        gyro[1] = g.gyro.y;
        gyro[2] = g.gyro.z;

        // Convert from the module's raw temperature units to Celsius.
        temp = t.temperature / 16.0 + 27.5;
#if defined(DEBUG_VERBOSE_SENSORS)
        Serial.printf( "Debug: Inertia read: accel=(%f,%f,%f)\r\n",
            accel[0], accel[1], accel[2] );
        Serial.printf( "Debug: Inertia read: mag=(%f,%f,%f)\r\n",
            mag[0], mag[1], mag[2] );
        Serial.printf( "Debug: Inertia read: gyro=(%f,%f,%f)\r\n",
            gyro[0], gyro[1], gyro[2] );
        Serial.printf( "Debug: Inertia read: temp=%f\r\n", temp );
#endif
    }

    /**
     * Returns the current water pressure and depth.
     *
     * @param[out] pressure
     *   The returned pressure, in mbar.
     * @param[out] depth
     *   The returned depth, in meters.
     *
     * @see isPressureSensorPresent()
     * @see https://github.com/bluerobotics/BlueRobotics_MS5837_Library
     */
    static inline void getWaterPressure( float& pressure, float& depth )
    {
        if ( !isPressureSensorPresent( ) )
        {
            // No sensor. Return zeroes.
            pressure = 0.0;
            depth = 0.0;
            return;
        }

        // Read the sensor. Can take up to 40ms.
        pressureSensor.read( );

        pressure = pressureSensor.pressure( );
        depth    = pressureSensor.depth( );
#if defined(DEBUG_VERBOSE_SENSORS)
        Serial.printf( "Debug: Pressure read: pressure=%f, depth=%f\r\n",
            pressure, depth );
#endif

        // Ignore pressure sensor's low-precision water temperature.
    }

    /**
     * Returns the current water temperature.
     *
     * @param[out] temp
     *   The returned temperature, in Celsius.
     *
     * @see isTemperatureSensorPresent()
     * @see https://github.com/bluerobotics/BlueRobotics_TSYS01_Library
     */
    static inline void getWaterTemperature( float& temp )
    {
        if ( !isTemperatureSensorPresent( ) )
        {
            // No sensor. Return zero.
            temp = 0.0;
            return;
        }

        temperatureSensor.read( );
        temp = temperatureSensor.temperature( );
#if defined(DEBUG_VERBOSE_SENSORS)
        Serial.printf( "Debug: Temp read: %f\r\n", temp );
#endif
        if ( temp <= BAD_WATER_TEMPERATURE )
            temp = 0.0;
    }
};
