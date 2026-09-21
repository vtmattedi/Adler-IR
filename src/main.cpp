#include <Arduino.h>
#include <NightMareNetwork.h>
#include <ArduinoJson.h>
#include <Version.h>
#include <board.h>
#include <boardinfo.h>
#include <TempSensor.h>
#include <IrController.h>
#include <NetworkSensor.h>
#include <Net.h>
#include <AcController.h>

// Adler: the air-conditioner controller.
//   sensors     DS18B20 (local), the IR receiver (local), the door (another device, over MQTT)
//   actuator    the IR transmitter
//   controller  AcController -- target temperature, door pause, morning shutdown --
//               speaking the Dashboard's AcController Service vocabulary on the console
//               and publishing its state document on <Device>/state.

static NetworkSensor doorSensor("door");

// ---- aggregators ------------------------------------------------------------------

static String sensorsReportJson()
{
    DynamicJsonDocument doc(256);
    JsonObject root = doc.to<JsonObject>();
    tempSensorReport(root);
    irSensorReport(root);
    String out;
    serializeJson(doc, out);
    return out;
}

/// The declaration the backend reads with the bare `sensors` command. Local sensors only: the
/// door is another device's reading and is declared there, not here.
static String sensorsDeclarationJson()
{
    DynamicJsonDocument doc(768);
    JsonObject root = doc.to<JsonObject>();
    tempSensorInfo(root);
    irSensorInfo(root);
    String out;
    serializeJson(doc, out);
    return out;
}

static String infoJson()
{
    DynamicJsonDocument doc(2048);
    doc["device"] = getDeviceName();
    doc["firmware"] = VERSION;
    JsonObject board = doc.createNestedObject("board");
    board["name"] = BOARD_NAME;
    JsonArray connections = board.createNestedArray("connections");
    for (size_t i = 0; i < kBoardConnectionCount; i++)
    {
        JsonObject c = connections.createNestedObject();
        c["gpio"] = kBoardConnections[i].gpio;
        c["device"] = kBoardConnections[i].device;
        c["level"] = digitalRead(kBoardConnections[i].gpio);
        c["notes"] = kBoardConnections[i].notes;
    }
    JsonObject sensors = doc.createNestedObject("sensors");
    tempSensorInfo(sensors);
    irSensorInfo(sensors);
    JsonObject actuators = doc.createNestedObject("actuators");
    irActuatorInfo(actuators);
    JsonObject controllers = doc.createNestedObject("controllers");
    gAc.info(controllers);
    String out;
    serializeJson(doc, out);
    return out;
}

static void publishSensors()
{
    MQTT_Send("/sensors", sensorsReportJson());
    irSensorMarkPublished();
}

/// Publish on change: the temperature moved a step worth seeing, the thermometer appeared or
/// vanished, or the receiver heard a remote. The 60 s heartbeat covers the rest.
static void watchForChange()
{
    static float lastTemp = NAN;
    static bool lastConnected = false;
    TempSensorStatus t = tempSensorStatus();
    bool tempMoved = (isnan(lastTemp) != isnan(t.tempC)) || (!isnan(t.tempC) && fabsf(t.tempC - lastTemp) >= 0.25f);
    bool irHeard = irSensorStatus().pendingPublish;
    if (tempMoved || t.connected != lastConnected || irHeard)
    {
        lastTemp = t.tempC;
        lastConnected = t.connected;
        publishSensors();
    }
}

// ---- pin-level debug helpers ------------------------------------------------------

/// Explicit digit checking rather than toInt(): toInt() returns 0 for junk and GPIO0 is a real
/// pin, so "SETPIN foo H" would otherwise silently drive GPIO0.
static bool parsePin(const String &text, uint8_t &pin)
{
    if (!text.length())
        return false;
    for (size_t i = 0; i < text.length(); i++)
        if (!isDigit(text[i]))
            return false;
    long value = text.toInt();
    if (value < 0 || value > BOARD_GPIO_MAX)
        return false;
    pin = (uint8_t)value;
    return true;
}

static String unusablePinMessage(uint8_t pin)
{
    return "GPIO" + String(pin) + " is not usable on this board: the flash and USB-console pins are reserved.";
}

// ---- the resolver -------------------------------------------------------------------

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    NightMareResults res;
    res.result = false;
    res.response = "not implemented";

    if (gAc.handles(message.command))
        return gAc.command(message);

    if (message.command == "SENSORS")
    {
        // Bare `sensors` -- no subcommand -- is the declaration the backend asks for on
        // discovery. The subcommands are the human-facing views of the same data.
        if (message.subcommand == "" || message.subcommand == "INFO")
        {
            res.result = true;
            res.response = sensorsDeclarationJson();
        }
        else if (message.subcommand == "REPORT" || message.subcommand == "DATA")
        {
            res.result = true;
            res.response = sensorsReportJson();
        }
        else
            res.response = "Unknown SENSORS subcommand available: [REPORT, INFO].";
    }
    else if (message.command == "INFO")
    {
        res.result = true;
        res.response = infoJson();
    }
    else if (message.command == "IR")
    {
        if (message.subcommand == "SEND")
        {
            uint32_t code = getIrCode(message.args[1]);
            if (code == 0)
                res.response = "Unknown IR code '" + message.args[1] + "'. Known: " + getIrCodeNames();
            else
            {
                res.result = sendIRCode(code);
                res.response = res.result ? "Queued " + getIrName(code) + " (0x" + String(code, HEX) + ")"
                                          : "IR queue is full, " + getIrName(code) + " dropped.";
            }
        }
        else if (message.subcommand == "LIST")
        {
            res.result = true;
            res.response = getIrCodeNames();
        }
        else if (message.subcommand == "INFO")
        {
            res.result = true;
            res.response = IrInfoJson();
        }
        else if (message.subcommand == "STATE")
        {
            res.result = true;
            res.response = acIrState.toJson();
        }
        else if (message.subcommand == "DEBUG")
        {
            String a = message.args[1];
            a.toUpperCase();
            if (a == "ON" || a == "1")
                enableIrDebug(true);
            else if (a == "OFF" || a == "0")
                enableIrDebug(false);
            else
            {
                res.response = "Unknown DEBUG argument. Use [ON, OFF].";
                return res;
            }
            res.result = true;
            res.response = "IR debug " + String(getIrDebug() ? "ON" : "OFF");
        }
        else
            res.response = "Unknown IR subcommand available: [SEND <name>, LIST, INFO, STATE, DEBUG ON|OFF].";
    }
    else if (message.command == "DS18")
    {
        TempSensorStatus s = tempSensorStatus();
        if (message.subcommand == "READ")
        {
            res.result = !isnan(s.tempC);
            if (res.result)
                res.response = String(s.tempC, 2);
            else if (s.connected)
                res.response = "Sensor found, first conversion still running.";
            else
                res.response = "No DS18B20 found on GPIO" + String(DS18B20_PIN) + ".";
        }
        else if (message.subcommand == "STATUS")
        {
            DynamicJsonDocument doc(256);
            doc["connected"] = s.connected;
            doc["pin"] = DS18B20_PIN;
            doc["address"] = s.address;
            doc["parasite"] = s.parasite;
            doc["temperature"] = s.tempC;
            if (s.lastReadMs)
                doc["age_ms"] = millis() - s.lastReadMs;
            String json;
            serializeJson(doc, json);
            res.response = json;
            res.result = true;
        }
        else if (message.subcommand == "PROBE")
        {
            uint8_t pin = 0;
            if (!parsePin(message.args[1], pin))
                res.response = "Usage: DS18 PROBE <pin>";
            else if (!isUsableGpio(pin))
                res.response = unusablePinMessage(pin);
            else
            {
                res.result = true;
                res.response = Ds18ProbeJson(pin);
            }
        }
        else
            res.response = "Unknown DS18 subcommand available: [READ, STATUS, PROBE <pin>].";
    }
    else if (message.command == "SETPIN")
    {
        // No subcommand keyword: the pin is args[0], which the parser also exposes as subcommand.
        uint8_t pin = 0;
        String level = message.args[1];
        level.toUpperCase();
        if (!parsePin(message.subcommand, pin))
            res.response = "Usage: SETPIN <pin> <H|L>";
        else if (!isUsableGpio(pin))
            res.response = unusablePinMessage(pin);
        else if (level == "H" || level == "HIGH" || level == "1" || level == "L" || level == "LOW" || level == "0")
        {
            bool high = (level == "H" || level == "HIGH" || level == "1");
            pinMode(pin, OUTPUT);
            digitalWrite(pin, high ? HIGH : LOW);
            res.result = true;
            res.response = "GPIO" + String(pin) + " driven " + (high ? "HIGH" : "LOW");
            const char *device = boardDeviceOnPin(pin);
            if (device != nullptr)
                res.response += " (note: " + String(device) + " is on this pin, so it is no longer under its own control)";
        }
        else
            res.response = "Unknown level '" + message.args[1] + "'. Use [H, L].";
    }
    else if (message.command == "BOARDINFO")
    {
        res.result = true;
        res.response = BoardInfoJson();
    }
    else if (message.command == "NET")
    {
        res.result = true;
        res.response = String("{\"droppedMessages\":") + Net_droppedMessages() + "}";
    }
    else if (message.command == "HELP")
    {
        res.result = true;
        res.response = "Groups: AC, DOOR, IR, DS18, SENSORS. Root: INFO, SETTEMP, TARGET, POWER, MANUALSYNC, "
                       "SLEEP-IN, SLEEP, SENDIR, PAUSEDOORSENSOR, SETPIN, BOARDINFO, NET, HELP. "
                       "<GROUP> HELP for details.";
    }
    else
        res.response = "Unknown command available: [AC, DOOR, IR, DS18, SENSORS, INFO, SETPIN, BOARDINFO, NET, HELP].";

    return res;
}

// ---- lifecycle -------------------------------------------------------------------------

void onWifiConnected(bool firstConnection)
{
    Serial.println("WiFi Connected!");
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}

void onMqttConnected()
{
    // Runs on the MQTT task. Publishing is safe -- fresh Strings, thread-safe client -- but
    // nothing here touches application state.
    MQTT_Send("/info", infoJson());
    MQTT_Send("/state", gAc.stateJson());
}

void setup()
{
    Config.begin(); // first: the device name and every module's settings come from it
    Serial.begin(115200);
    Serial.println(getDeviceName());
    Serial.printf("\tFirmware Version: %s\n", VERSION);
    Serial.printf("\tBuild Date: %s\n", BUILD_TIMESTAMP);
    printBoardInfo();
    Serial.println("Starting NightMare Network...");

    setCommandResolver(localHandleNightMareCommand);
    WiFi_onConnected(onWifiConnected);
    WiFi_Auto();

    // The inbox before MQTT connects, so the first retained messages land in it.
    doorSensor.loadBinding();
    Net_registerSensor(&doorSensor);
    Net_begin();
    MQTT_onConnected(onMqttConnected);

    setupTempSensor();
    startIrServices();
    startAcController(currentTemperature, &doorSensor);

    Timers.create("sensors_heartbeat", 60, publishSensors);
    Timers.create("sensors_watch", 1, watchForChange);
}

void loop()
{
    Timers.run();
    scheduler.run();
    Net_loop();
    NightMareCommand_SerialResolver(&Serial, '\n');
}
