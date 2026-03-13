#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NightMareNetwork.h>
#define ONE_WIRE_BUS 16 // DS18b20 bus pin.
#define TEMPERATURE_PRECISION 12 // DS18B20 supports 9-12 bit precision, we will use 12 for maximum resolution
String SensorsInfoJson();
String SensorsDataJson();
void startSensors();
double getTemperature();