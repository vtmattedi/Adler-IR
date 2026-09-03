#include "Sensors.h"
#if TEMPERATURE_PRECISION < 9 || TEMPERATURE_PRECISION > 12
#error "Invalid TEMPERATURE_PRECISION value. Must be between 9 and 12."
#endif
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int _timeToConversion = 750; // max conversion time in ms for 12 bit resolution
double _sensorTemperature = 0.0;
unsigned long _lastConversionRequest = 0;

// Returns the average analog Read of the given pin over the specified period in milliseconds. 
// This is used to get a more stable reading from the ZMPT101B AC voltage sensor.
// and of the ACS712 current sensor.
int getZeroCrossingTime(unsigned int analogPin, unsigned long periodMs)
{
    uint32_t Vsum = 0;
    uint32_t measurements_count = 0;
    uint32_t t_start = micros();

    while (micros() - t_start < periodMs * 1000)
    {
        Vsum += analogRead(analogPin);
        measurements_count++;
    }

    return Vsum / measurements_count;
}

// this task will continously read current, voltage (then calculate power) of the AC voltage/current sensors since they need need to be read over a period of time to get an accurate reading, and we don't want to block the main loop while doing that.
void continuousReadSensorsTask(void *pvParameters)
{
    #define MAINS_FREQUENCY 60 // in Hz, used to calculate the expected zero crossing time for the AC voltage sensor
    #define MAINS_PERIOD_MS (1000 / MAINS_FREQUENCY) // in ms, the period of the AC mains voltage
    #define TASK_DELAY_MS 1000 // in ms, how often to read the sensors and update the readings. We can read them every second since we don't need very high frequency updates for our use case.
    int ZMPT101B_zeroCrossingValue = getZeroCrossingTime(ZMPT101B_BUS, MAINS_PERIOD_MS); // read for 1 second to get a stable reading
    int ACS712_zeroCrossingValue = getZeroCrossingTime(ACS712_BUS, MAINS_PERIOD_MS); // read for 1 second to get a stable reading
    for (;;)
    {
        //first read voltage.
        int raw_analog_value = 0;
        uint32_t measurements_count = 0;
        uint32_t t_start = micros();
        // Read for a full period of the AC mains voltage to get an accurate reading. 
        while (micros() - t_start < MAINS_PERIOD_MS * 1000)
        {
            int read = analogRead(ZMPT101B_BUS);
            raw_analog_value += _abs(read - ZMPT101B_zeroCrossingValue); // we want to measure the amplitude of the voltage, so we take the absolute value of the difference from the zero crossing value
            measurements_count++;
        }
        
    }

}


void updateSensors()
{
    bool conversionComplete = DS18B20.isConversionComplete();
    if (conversionComplete || (millis() - _lastConversionRequest > (unsigned long)_timeToConversion))
    {
        _sensorTemperature = DS18B20.getTempCByIndex(0);
        DS18B20.requestTemperatures();
        _lastConversionRequest = millis();
    }
}

void startSensors()
{
    // Initialize the OneWire bus and the DS18B20 sensor
    pinMode(ONE_WIRE_BUS, INPUT);
    DS18B20.begin();
    DS18B20.setWaitForConversion(false);          // We will handle waiting for conversion ourselves in the updateSensors function
    DS18B20.setResolution(TEMPERATURE_PRECISION); // Set the resolution to 12 bits (max) since we will be handling the timing ourselves, we might as well get the best resolution possible
    DS18B20.requestTemperatures();
    _lastConversionRequest = millis();
    _timeToConversion = DS18B20.millisToWaitForConversion();
    // Start a timer to update the sensor readings every second
    Timers.create("Sensor Update Timer", 1000, updateSensors, true);
}

double getTemperature()
{
    return _sensorTemperature;
}

String SensorsInfoJson()
{
    String json = "{";
    json += "\"type\": \"DS18B20\",";
    json += "\"resolution\": " + String(TEMPERATURE_PRECISION) + ",";
    json += "\"parasitePower\": " + String(DS18B20.isParasitePowerMode() ? "true" : "false");
    json += "}";
    return json;
}

String SensorsDataJson()
{
    String json = "{";
    json += "\"temperature\": " + String(getTemperature(), 2);
    json += "}";
    return json;
}