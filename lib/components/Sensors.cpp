#include "Sensors.h"
#if TEMPERATURE_PRECISION < 9 || TEMPERATURE_PRECISION > 12
#error "Invalid TEMPERATURE_PRECISION value. Must be between 9 and 12."
#endif
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int _timeToConversion = 750; // max conversion time in ms for 12 bit resolution
double _sensorTemperature = 0.0;
unsigned long _lastConversionRequest = 0;
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