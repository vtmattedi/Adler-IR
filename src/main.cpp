#include <Arduino.h>
#include "NightmareNetwork.h"
#include <ArduinoJson.h>
#include <AdlerComponents.h>
#include <Version.h>
#include "board.h"
#include "D:\nightmaresystems\esp32\adler\.pio\libdeps\m5stack-nanoc6\NightMareNetwork\src\Core\buttons.h"
// #include "PowerMeter.h"
#ifdef ESP32
#define ONE_WIRE_BUS 16 // DS18b20 bus pin.
#define LED_PIN 2       // LED pin. (onboard led is pin 2 on ESP-01S boards).
#define LDR_PIN 33      // LDR pin (ANALOG).
#define PZEM_RX_PIN 23  // PZEM RX pin (to ESP32 TX pin).
#define PZEM_TX_PIN 22  // PZEM TX pin (to ESP32 RX pin).
#endif

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    NightMareResults res;
    res.result = false;
    res.response = "not implemented";

    if (message.command == "SENSORS")
    {
        res.result = true;
        if (message.subcommand == "INFO")
            res.response = SensorsInfoJson();
        else if (message.subcommand == "DATA")
            res.response = SensorsDataJson();
        else
            res.response = "Unknown SENSORS subcommand available: [INFO, DATA].";
    }
    else if (message.command == "READ")
    {
        auto t_init = micros();

        auto t_end = micros();
        res.result = true;
        res.response = String(t_end - t_init) + " microseconds";
    }
    else if (message.command == "AC")
    {
        if (message.subcommand == "STATE")
        {
            res.result = true;
            const IrState &currentState = irController.state;
            DynamicJsonDocument doc(1024);
            doc["power"] = currentState.power;
            doc["temp"] = currentState.temp;
            doc["mode"] = currentState.mode;
            doc["fan"] = currentState.fan;
            doc["turbo"] = currentState.turbo;
            doc["led"] = currentState.led;
            String msg;
            serializeJson(doc, msg);
            res.response = msg;
        }
        else if (message.subcommand == "POWER")
        {
            if (message.args[1] == "ON" || message.args[1] == "0")
                irController.setPower(true);
            else if (message.args[1] == "OFF" || message.args[1] == "1")
                irController.setPower(false);
            else if (message.args[1] == "TOGGLE" || message.args[1] == "2")
                irController.setPower(!irController.state.power);
            else
            {
                res.result = false;
                res.response = "Unknown POWER argument. Use [ON, OFF, TOGGLE].";
                return res;
            }
            res.result = true;
            res.response = "OK: " + String(irController.state.power ? "ON" : "OFF");
        }
        else if (message.subcommand == "TEMP")
        {
            int temp = message.args[1].toInt();
            if (temp < MIN_AC_TEMP || temp > MAX_AC_TEMP)
            {
                res.result = false;
                res.response = "Temperature out of range. Valid range is " + String(MIN_AC_TEMP) + "-" + String(MAX_AC_TEMP) + "°C.";
                return res;
            }
            irController.setTemp(temp);
            res.result = true;
            res.response = "OK: " + String(irController.state.temp);
        }
        else
        {
            res.result = false;
            res.response = "Unknown AC subcommand available: [STATE, POWER, TEMP].";
        }
    }
    else if (message.command == "POWER")
    {
        if (message.subcommand == "DATA")
        {
            res.result = true;
            // res.response = gPowerMeter.getDataJson();
        }
        else if (message.subcommand == "READ")
        {
            // res.result = gPowerMeter.readNow();
            // if (res.result)
            //     res.response = gPowerMeter.getDataJson();
            // else
            //     res.response = "Power meter read failed.";
        }
        else
        {
            res.result = false;
            res.response = "Unknown POWER subcommand available: [DATA, READ].";
        }
    }
    else
    {
        res.result = false;
        res.response = "Unknown command available: [SENSORS, AC, POWER].";
    }
    String raw = message.command + " " + message.subcommand + " " + message.args[1] + " " + message.args[2] + " " + message.args[3] + " " + message.args[4];
    Serial.printf("input = %s, result = <%s>\n", raw.c_str(), OK_LOG(res.result));
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
    rgbLedWrite(0x00ff); // blue
    Timers.setTimeout([]()
                      {
                          rgbLedWrite(0x0000); // off
                      },
                      5000, true);
}
float ramUsagePercent()
{
    size_t totalHeap = ESP.getHeapSize();   // Total heap
    size_t freeHeap = ESP.getFreeHeap();    // Free heap
    size_t usedHeap = totalHeap - freeHeap; // Used heap
    float percentUsed = ((float)usedHeap / (float)totalHeap) * 100.0;
    return percentUsed;
}

void sensors()
{
    float temperature = getTemperature();
    MQTT_Send("/sensors/temperature", String(temperature));
    DynamicJsonDocument doc(1024);
    const IrState &currentState = irController.state;
    doc["power"] = currentState.power;
    doc["temp"] = currentState.temp;
    doc["turbo"] = currentState.turbo;
    String msg;
    serializeJson(doc, msg);
}

void telemetry()
{
    DynamicJsonDocument doc(1024);
#ifdef COMPILE_HTTP_SERVER
    bool httpDirect = getHttpState() > 0;
#else
    bool httpDirect = false; // TODO: implement direct http and set this to true when it's implemented and enabled.
#endif
    JsonObject system = doc.createNestedObject("System");
    system["Uptime"] = millis() / 1000;
    system["FreeHeap"] = ramUsagePercent();
    system["boot_time"] = SystemSettings.get("boot_time");
    system["time_synced"] = SystemSettings.getFlag("time_synced");
    system["reset_reason"] = esp_reset_reason();
    system["wifi_rssi"] = WiFi.RSSI();
    system["mqtt_connection"] = MQTT_isLocal() ? "Local" : "Remote";
    system["ip_address"] = WiFi.localIP().toString();
    system["direct_http"] = httpDirect;
    String msg;
    serializeJson(doc, msg);
    MQTT_Send("/ai_state", msg);
    // MQTT_Send("/power", gPowerMeter.getDataJson());
}

void handleButton(ButtonEvent event)
{
    Serial.printf("Button event: %s\n", getButtonEventName(event));
    if (event == BUTTON_EVENT_CLICKED)
    {
        sendIRCode(POWER);
        rgbLedWrite(0x00ff);                                // blue
        digitalWrite(PIN_ONBOARD_BLUE_LED, ONBOARD_LED_ON); // power on the RGB LED
        Timers.setTimeout([]()
                          {
                              rgbLedWrite(0x0000);                                // off
                              digitalWrite(PIN_ONBOARD_BLUE_LED, ONBOARD_LED_OFF); // power on the RGB LED
                          },
                          5000, true);
    }
}

void setup()
{
    pinMode(PIN_ONBOARD_BLUE_LED, OUTPUT);
    pinMode(PIN_RGB_POWER, OUTPUT);
    rgbLedWrite(0xff0000); // Red
    Serial.begin(115200);
    Serial.println(DEVICE_NAME);
    Serial.printf("\tFirmware Version: %s\n", VERSION);
    Serial.printf("\tBuild Date: %s\n", BUILD_TIMESTAMP);
    Serial.println("Starting NightMare Network...");
    setCommandResolver(localHandleNightMareCommand);
    WiFi_onConnected(onWifiConnected);
    WiFi_Auto();
    startSensors();

    startIrServices();
    startAcService();
    Timers.create("Telemetry Timer", 30, telemetry, false); // send telemetry every minute
    Timers.create("Sensor Timer", 5, sensors, false);       // send sensor data every second
    rgbLedWrite(0xff00);                                    // Green
    // pinMode(5, INPUT_PULLUP);
    createButtonOnPin(PIN_BUTTON, handleButton);
}

void loop()
{
    Timers.run();
    scheduler.run();
    NightMareCommand_SerialResolver(&Serial, '\n');
}