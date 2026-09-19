#include <Arduino.h>
#include <ArduinoJson.h>
#include <NightMare.h>
#include <Version.h>
#include <board.h>
#include <boardinfo.h>
#include <creds.h>
#include <AdlerSettings.h>
#include <TempSensor.h>
#include <IrController.h>
#include <DoorInput.h>
#include <AcController.h>
#include <math.h>

using namespace NightMare;

struct ManualSyncArgs { bool power; uint8_t temperature; };
struct DoorSourceArgs { String owner; String resource; bool invert; };
struct AcConfigArgs { String key; String value; };

namespace NightMare {
template<> struct NetCodec<ManualSyncArgs> {
    static String encode(const ManualSyncArgs& v) {
        return String("[") + (v.power ? "true" : "false") + "," + v.temperature + "]";
    }
    static bool decode(const String& text, ManualSyncArgs& out) {
        StaticJsonDocument<96> doc;
        if (deserializeJson(doc, text) || !doc.is<JsonArray>() || doc.size() != 2 ||
            !doc[0].is<bool>() || !doc[1].is<int>()) return false;
        int temp = doc[1].as<int>();
        if (temp < MIN_AC_TEMP || temp > MAX_AC_TEMP) return false;
        out = {doc[0].as<bool>(), static_cast<uint8_t>(temp)};
        return true;
    }
};
template<> struct NetCodec<DoorSourceArgs> {
    static String encode(const DoorSourceArgs& v) {
        StaticJsonDocument<192> doc;
        JsonArray array = doc.to<JsonArray>();
        array.add(v.owner); array.add(v.resource); array.add(v.invert);
        String result; serializeJson(doc, result); return result;
    }
    static bool decode(const String& text, DoorSourceArgs& out) {
        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, text) || !doc.is<JsonArray>() || doc.size() != 3 ||
            !doc[0].is<const char*>() || !doc[1].is<const char*>() || !doc[2].is<bool>()) return false;
        out = {doc[0].as<String>(), doc[1].as<String>(), doc[2].as<bool>()};
        return true;
    }
};
template<> struct NetCodec<AcConfigArgs> {
    static String encode(const AcConfigArgs& v) {
        StaticJsonDocument<192> doc;
        JsonArray array = doc.to<JsonArray>();
        array.add(v.key); array.add(v.value);
        String result; serializeJson(doc, result); return result;
    }
    static bool decode(const String& text, AcConfigArgs& out) {
        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, text) || !doc.is<JsonArray>() || doc.size() != 2 ||
            !doc[0].is<const char*>() || !doc[1].is<const char*>()) return false;
        out = {doc[0].as<String>(), doc[1].as<String>()};
        return true;
    }
};
} // namespace NightMare

static MqttTransport transport;
static NightMare::Network* network = nullptr;
static Console* console = nullptr;
static WifiStation wifi;
static OtaService ota;
static Runtime runtime;
static DoorInput door;
static String deviceId, wifiSsid, wifiPassword;
static bool mqttStarted = false, otaStarted = false;
static uint32_t restartAtMs = 0;

static ResourceManager& resources() { return network->resources(); }

static ResourceMetadata temperatureMeta = {"Room temperature", "C", nullptr, "sensor"};
static ResourceMetadata targetMeta = {"Target temperature", "C", nullptr, "ac", "-30", "30"};
static ResourceMetadata unitTempMeta = {"AC set temperature", "C", nullptr, "ac", "18", "30"};
static NetValue<float> temperature("temperature", NetAccess::READ, &temperatureMeta);
static NetValue<bool> temperatureConnected("temperatureConnected");
static NetValue<String> temperatureAddress("temperatureAddress");
static NetValue<String> irLastCode("irLastCode");
static NetValue<uint32_t> irRawCode("irRawCode");
static NetValue<String> firmwareVersion("firmwareVersion");
static NetValue<String> boardName("boardName");
static NetValue<uint32_t> uptime("uptime");
static NetValue<int8_t> acState("acState");
static NetValue<bool> acPower("acPower", NetAccess::READ_WRITE);
static NetValue<uint8_t> acUnitTemperature("acUnitTemperature", NetAccess::READ_WRITE, &unitTempMeta);
static NetValue<uint8_t> acMode("acMode", NetAccess::READ_WRITE);
static NetValue<double> acTarget("acTarget", NetAccess::READ_WRITE, &targetMeta);
static NetValue<bool> sleepIn("sleepIn", NetAccess::READ_WRITE);
static NetValue<bool> doorPaused("doorPaused", NetAccess::READ_WRITE);
static NetValue<uint8_t> doorState("doorState");
static NetValue<uint32_t> sleepRemaining("sleepRemaining");
static NetValue<bool> irDebug("irDebug", NetAccess::READ_WRITE);

static const ResourceMetadata::Field syncFields[] = {
    {"power", NetValueType::BOOL}, {"temperature", NetValueType::UINT8}
};
static ResourceMetadata syncMeta = {"Correct AC belief", nullptr, nullptr, "ac", nullptr, nullptr, syncFields, 2};
static const ResourceMetadata::Field doorFields[] = {
    {"owner", NetValueType::STRING}, {"resource", NetValueType::STRING}, {"invert", NetValueType::BOOL}
};
static ResourceMetadata doorMeta = {"Configure remote door", nullptr, "Applies after restart", "door", nullptr, nullptr, doorFields, 3};
static const ResourceMetadata::Field configFields[] = {
    {"key", NetValueType::STRING}, {"value", NetValueType::STRING}
};
static ResourceMetadata configMeta = {"Configure AC policy", nullptr, "Allowed keys are documented in Adler README", "ac", nullptr, nullptr, configFields, 2};
static NetAction<String> sendIr("sendIr", ActionResponse::ACK);
static NetAction<ManualSyncArgs> manualSync("manualSync", ActionResponse::ACK, &syncMeta);
static NetAction<uint32_t> setSleepMinutes("setSleepMinutes", ActionResponse::ACK);
static NetAction<bool> morningOff("morningOff", ActionResponse::ACK);
static NetAction<void> rescanTemperature("rescanTemperature", ActionResponse::ACK);
static NetAction<uint8_t> probeDs18("probeDs18", ActionResponse::RESULT);
static NetAction<DoorSourceArgs> configureDoor("configureDoor", ActionResponse::ACK, &doorMeta);
static NetAction<AcConfigArgs> configureAc("configureAc", ActionResponse::ACK, &configMeta);
static NetEvent<String> irReceived("irReceived");
static NetEvent<void> temperatureLost("temperatureLost");

static void syncResources() {
    auto& r = resources();
    const TempSensorStatus sensor = tempSensorStatus();
    r.set(temperatureConnected, sensor.connected);
    if (sensor.connected && !isnan(sensor.tempC)) r.set(temperature, sensor.tempC);
    r.set(temperatureAddress, String(sensor.address));
    const IrSensorStatus ir = irSensorStatus();
    if (ir.everReceived) {
        r.set(irLastCode, String(ir.name));
        r.set(irRawCode, ir.code);
    }
    r.set(acState, static_cast<int8_t>(gAc.state()));
    r.set(acPower, acIrState.state.power);
    r.set(acUnitTemperature, acIrState.state.temp);
    r.set(acMode, static_cast<uint8_t>(acIrState.state.mode));
    r.set(acTarget, gAc.target());
    r.set(sleepIn, gAc.sleepIn());
    r.set(doorPaused, gAc.doorPaused());
    r.set(doorState, gAc.doorState());
    r.set(sleepRemaining, gAc.sleepRemainingSeconds());
    r.set(irDebug, getIrDebug());
}

static void pollResources(void*) {
    syncResources();
    resources().set(uptime, static_cast<uint32_t>(millis() / 1000UL));
}

static void pollTemperature(void*) {
    static bool wasConnected = false;
    tickTempSensor();
    bool connected = tempSensorStatus().connected;
    if (wasConnected && !connected) resources().emit(temperatureLost);
    wasConnected = connected;
}

static void onIrReceived(uint32_t code, const String& name) {
    resources().set(irLastCode, name);
    resources().set(irRawCode, code);
    resources().emit(irReceived, name);
}

static ActionStatus writeValue(void*, NetResource& resource, const String& text) {
    bool boolean;
    uint8_t small;
    double number;
    if (&resource == &acPower) {
        if (!NetCodec<bool>::decode(text, boolean)) return ActionStatus::INVALID_ARGUMENT;
        gAc.setPower(boolean);
        syncResources();
        return acPower.get() == boolean ? ActionStatus::OK : ActionStatus::REJECTED;
    }
    if (&resource == &acUnitTemperature) {
        if (!NetCodec<uint8_t>::decode(text, small) || small < MIN_AC_TEMP || small > MAX_AC_TEMP)
            return ActionStatus::INVALID_ARGUMENT;
        gAc.setUnitTemperature(small);
        syncResources();
        return acUnitTemperature.get() == small ? ActionStatus::OK : ActionStatus::REJECTED;
    }
    if (&resource == &acMode) {
        if (!NetCodec<uint8_t>::decode(text, small) || small > AC_MODE_HUMIDIFIER)
            return ActionStatus::INVALID_ARGUMENT;
        acIrState.setMode(static_cast<AcIrMode>(small));
        syncResources();
        return acMode.get() == small ? ActionStatus::OK : ActionStatus::REJECTED;
    }
    if (&resource == &acTarget) {
        if (!NetCodec<double>::decode(text, number) || fabs(number) < 16 || fabs(number) > 35)
            return ActionStatus::INVALID_ARGUMENT;
        gAc.setTarget(number);
    } else if (&resource == &sleepIn) {
        if (!NetCodec<bool>::decode(text, boolean)) return ActionStatus::INVALID_ARGUMENT;
        gAc.setSleepIn(boolean);
    } else if (&resource == &doorPaused) {
        if (!NetCodec<bool>::decode(text, boolean)) return ActionStatus::INVALID_ARGUMENT;
        gAc.setDoorPaused(boolean);
    } else if (&resource == &irDebug) {
        if (!NetCodec<bool>::decode(text, boolean) || !settings.set("ir_debug", boolean ? "1" : "0"))
            return ActionStatus::INVALID_ARGUMENT;
        enableIrDebug(boolean);
    } else return ActionStatus::REJECTED;
    syncResources();
    return ActionStatus::OK;
}

static bool validId(const String& id) {
    if (!id.length() || id.length() > 64) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        char ch = id[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

static bool validClock(const String& value) {
    return value.length() == 5 && isDigit(value[0]) && isDigit(value[1]) &&
           value[2] == ':' && isDigit(value[3]) && isDigit(value[4]) &&
           value.substring(0, 2).toInt() < 24 && value.substring(3).toInt() < 60;
}

static bool validAcSetting(const AcConfigArgs& args) {
    const String& key = args.key;
    const String& value = args.value;
    if (key == "ac_turn_off_time" || key == "ac_sleep_in_turn_off_time") return validClock(value);
    if (key == "ac_door_control") return value == "0" || value == "1";
    double number;
    if (!NetCodec<double>::decode(value, number)) return false;
    if (key == "ac_hysteresis") return number >= 0 && number <= 5;
    if (key == "ac_door_pause_secs" || key == "ac_door_stop_secs")
        return floor(number) == number && number >= 0 && number <= 36000;
    if (key == "ac_default_target") return number >= 16 && number <= 35;
    if (key == "ac_utc_offset_minutes") return floor(number) == number && number >= -720 && number <= 840;
    return false;
}

static ActionStatus handleAction(void*, NetResource& resource, const String& text, String& result) {
    if (&resource == &sendIr) {
        String name;
        if (!sendIr.parse(text, name)) return ActionStatus::INVALID_ARGUMENT;
        uint32_t code = getIrCode(name);
        if (!code) return ActionStatus::INVALID_ARGUMENT;
        if (!sendIRCode(code)) return ActionStatus::BUSY;
    } else if (&resource == &manualSync) {
        ManualSyncArgs args;
        if (!manualSync.parse(text, args)) return ActionStatus::INVALID_ARGUMENT;
        gAc.manualSync(args.power, args.temperature);
    } else if (&resource == &setSleepMinutes) {
        uint32_t minutes;
        if (!setSleepMinutes.parse(text, minutes) || minutes > 10080)
            return ActionStatus::INVALID_ARGUMENT;
        gAc.setSleepMinutes(minutes);
    } else if (&resource == &morningOff) {
        bool force;
        if (!morningOff.parse(text, force)) return ActionStatus::INVALID_ARGUMENT;
        gAc.morningOff(force);
    } else if (&resource == &rescanTemperature) {
        if (text.length()) return ActionStatus::INVALID_ARGUMENT;
        rescanTempSensor();
    } else if (&resource == &probeDs18) {
        uint8_t pin;
        if (!probeDs18.parse(text, pin) || !isUsableGpio(pin)) return ActionStatus::INVALID_ARGUMENT;
        result = Ds18ProbeJson(pin);
    } else if (&resource == &configureDoor) {
        DoorSourceArgs args;
        if (!configureDoor.parse(text, args) ||
            ((args.owner.length() || args.resource.length()) &&
             (!validId(args.owner) || !validId(args.resource) || args.owner == deviceId)))
            return ActionStatus::INVALID_ARGUMENT;
        if (!settings.set("door_device", args.owner) ||
            !settings.set("door_key", args.resource) ||
            !settings.set("door_invert", args.invert ? "1" : "0")) return ActionStatus::ERROR;
        restartAtMs = millis() + 1500; // Apply the new mirror after the ACK can leave.
    } else if (&resource == &configureAc) {
        AcConfigArgs args;
        if (!configureAc.parse(text, args) || !validAcSetting(args)) return ActionStatus::INVALID_ARGUMENT;
        if (!settings.set(args.key.c_str(), args.value)) return ActionStatus::ERROR;
        gAc.reloadSettings();
    } else return ActionStatus::REJECTED;
    syncResources();
    return ActionStatus::OK;
}

static bool registerResources() {
    auto& r = resources();
    bool ok = true;
    ok &= r.add(temperature, {true, 10000});
    ok &= r.add(temperatureConnected);
    ok &= r.add(temperatureAddress);
    ok &= r.add(irLastCode);
    ok &= r.add(irRawCode);
    ok &= r.add(firmwareVersion);
    ok &= r.add(boardName);
    ok &= r.add(uptime, {false, 60000});
    ok &= r.add(acState);
    ok &= r.add(acPower, {true, 60000});
    ok &= r.add(acUnitTemperature);
    ok &= r.add(acMode);
    ok &= r.add(acTarget);
    ok &= r.add(sleepIn);
    ok &= r.add(doorPaused);
    ok &= r.add(doorState);
    ok &= r.add(sleepRemaining, {false, 60000});
    ok &= r.add(irDebug);
    ok &= r.add(sendIr);
    ok &= r.add(manualSync);
    ok &= r.add(setSleepMinutes);
    ok &= r.add(morningOff);
    ok &= r.add(rescanTemperature);
    ok &= r.add(probeDs18);
    ok &= r.add(configureDoor);
    ok &= r.add(configureAc);
    ok &= r.add(irReceived);
    ok &= r.add(temperatureLost);
    if (!ok) return false;
    r.onWrite(acPower, writeValue);
    r.onWrite(acUnitTemperature, writeValue);
    r.onWrite(acMode, writeValue);
    r.onWrite(acTarget, writeValue);
    r.onWrite(sleepIn, writeValue);
    r.onWrite(doorPaused, writeValue);
    r.onWrite(irDebug, writeValue);
    r.onAction(sendIr, handleAction);
    r.onAction(manualSync, handleAction);
    r.onAction(setSleepMinutes, handleAction);
    r.onAction(morningOff, handleAction);
    r.onAction(rescanTemperature, handleAction);
    r.onAction(probeDs18, handleAction);
    r.onAction(configureDoor, handleAction);
    r.onAction(configureAc, handleAction);
    r.set(firmwareVersion, String(VERSION));
    r.set(boardName, String(BOARD_NAME));
    return true;
}

static void pollWifi(void*) {
    const bool newlyConnected = wifi.tick();
    static uint32_t lastMqttAttemptMs = 0;
    if (wifi.connected() && !mqttStarted &&
        (newlyConnected || static_cast<uint32_t>(millis() - lastMqttAttemptMs) >= 10000)) {
        lastMqttAttemptMs = millis();
        mqttStarted = transport.begin(MQTT_URI, MQTT_USER, MQTT_PASSWD);
        if (!mqttStarted) Serial.println("[net] MQTT start failed; retrying");
    }
    if (wifi.connected() && !otaStarted) otaStarted = ota.begin(deviceId.c_str(), OTA_PASSWORD);
    if (restartAtMs && static_cast<int32_t>(millis() - restartAtMs) >= 0) ESP.restart();
}

void setup() {
    Serial.begin(115200);
    if (!settings.begin()) Serial.println("[settings] LittleFS config unavailable or invalid");
    deviceId = settings.get("_device_name");
    if (!validId(deviceId)) {
        deviceId = "Adler-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
        settings.set("_device_name", deviceId);
    }
    wifiSsid = settings.get("_ssid", DEFAULT_SSID);
    wifiPassword = settings.get("_password", DEFAULT_PASSWORD);
    Serial.printf("%s firmware %s (%s)\n", deviceId.c_str(), VERSION, BUILD_TIMESTAMP);
    printBoardInfo();

    network = new NightMare::Network(deviceId.c_str(), transport);
    if (!network) { Serial.println("[net] allocation failed"); return; }
    console = new Console(resources());
    if (!console || !transport.attach(*network) || !registerResources()) {
        Serial.println("[net] initialization failed");
        return;
    }
    const String doorOwner = settings.get("door_device");
    const String doorId = settings.get("door_key", "door");
    if (doorOwner.length() && (doorOwner == deviceId || !door.begin(resources(), doorOwner, doorId,
                                          settings.get("door_invert", "0") == "1")))
        Serial.println("[door] invalid remote binding");

    setupTempSensor();
    setIrReceivedHandler(onIrReceived);
    startIrServices();
    gAc.begin(currentTemperature, &door, runtime.scheduler());
    syncResources();

    runtime.add(pollWifi);
    runtime.add([](void*) { network->tick(); });
    runtime.add([](void*) { console->tick(Serial); });
    runtime.add(pollTemperature);
    runtime.add([](void*) { tickIrServices(); });
    runtime.add([](void*) { ota.tick(); });
    runtime.scheduler().every("acLoop", 2000, [](void*) { gAc.loop(); syncResources(); });
    runtime.scheduler().every("resources", 1000, pollResources);
    wifi.begin(wifiSsid.c_str(), wifiPassword.c_str(), deviceId.c_str());
}

void loop() {
    if (network) runtime.tick();
    delay(2);
}
