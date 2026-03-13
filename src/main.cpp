#include <Arduino.h>
#include "NightmareNetwork.h"
#include <ArduinoJson.h>
#include <AdlerComponents.h>
#include <Version.h>
#ifdef ESP32
#define IR_SEND_PIN 19  // IR Emitter pin.
#define ONE_WIRE_BUS 16 // DS18b20 bus pin.
#define LED_PIN 2       // LED pin. (onboard led is pin 2 on ESP-01S boards).
#define LDR_PIN 33      // LDR pin (ANALOG).
#endif

NightMareResults localHandleNightMareCommand(String command)
{
    NightMareResults res;
    res.result = false;
    res.response = "not implement";
    return res;
}

String getSystemInfo()
{

    return "{}";
}

void onWifiConnected(bool firstConnection)
{
    Serial.println("WiFi Connected!");
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}

void setup()
{
    Config.clear(); 
    Serial.begin(115200);
    Serial.println(DEVICE_NAME);
    Serial.printf("\tFirmware Version: %s\n", VERSION);
    Serial.printf("\tBuild Date: %s\n", BUILD_TIMESTAMP);
    Serial.println("Starting NightMare Network...");
    WiFi_onConnected(onWifiConnected);
    WiFi_Auto();
    startIrServices();
    startSensors();
    startAcService();
}

void loop()
{
    Timers.run();
    scheduler.run();
    NightMareCommand_SerialResolver(&Serial, '\n');
}