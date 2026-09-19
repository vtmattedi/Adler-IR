#pragma once
#include <Arduino.h>
#include <board.h>

/// 1-Wire data pin for the DS18B20. The bus needs an external 4.7k pull-up to 3.3V: the ESP32
/// internal one (~45k) is too weak for it. Most 3-pin breakout modules already carry the resistor;
/// bare TO-92 parts and waterproof probes do not.
#define DS18B20_PIN PIN_ONE_WIRE
/// 9-12. 12 bits gives 0.0625 C steps and takes 750 ms to convert; each bit less halves both.
#define TEMP_RESOLUTION_BITS 12
/// Time from the start of one reading to the start of the next.
#define TEMP_READ_INTERVAL_MS 5000
/// Consecutive failed reads before the sensor is treated as gone and the bus is searched again.
/// A single CRC error is not worth reporting the temperature as unknown.
#define TEMP_MAX_FAILURES 3

/// @brief The latest sensor status, updated by the application's Runtime.
struct TempSensorStatus
{
    bool connected;      ///< Found on the bus and not yet written off by TEMP_MAX_FAILURES.
    bool parasite;       ///< Powered from the data line instead of VDD.
    float tempC;         ///< Same as currentTemperature(): NAN when there is no reading.
    uint32_t lastReadMs; ///< millis() of the last good reading, 0 if there has never been one.
    char address[17];    ///< ROM code in hex; empty while not connected.
};

/// @brief Initializes the bus; the Runtime then calls tickTempSensor().
void setupTempSensor();
void tickTempSensor();
void rescanTempSensor();
/// @brief Latest reading in Celsius, or NAN when there is none (not found yet, or lost).
/// Never blocks: it returns the latest completed conversion.
float currentTemperature();
/// @brief Snapshot of the sensor state.
TempSensorStatus tempSensorStatus();

/// @brief Debug probe: brings up a throwaway OneWire bus on an arbitrary pin, looks for a DS18B20
/// and reads it, without disturbing the configured sensor. Meant for finding which pin a sensor is
/// actually on. Blocks for the conversion (~750 ms), so it is a console command, not a poll.
String Ds18ProbeJson(uint8_t pin);
