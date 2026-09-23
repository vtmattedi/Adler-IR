#include "NetResources.h"

#include <ArduinoJson.h>
#include <NightMare.h>
#include <IrController/IrController.h>
String formatString(const char *format, ...)
{
    // Create a buffer to store the formatted string
    char buffer[1024]; // You can adjust the size as needed
    va_list args;
    va_start(args, format);
    // Format the string into the buffer
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (len < 0)
    {
        // Error occurred during formatting
        return String();
    }
    // Convert the formatted buffer to a String
    return String(buffer);
}
namespace
{
    const ActionArgMetadata kIrSendArguments[] = {
        {"code", NetValueType::STRING},
    };
    const ActionArgMetadata kAcManualSyncArguments[] = {
        {"power", NetValueType::BOOLEAN},
        {"temperature", NetValueType::INTEGER},
    };
    const ActionArgMetadata kAcSleepArguments[] = {
        {"minutes", NetValueType::INTEGER},
    };
    const ActionArgMetadata kAcMorningOffArguments[] = {
        {"force", NetValueType::BOOLEAN, false},
    };

    template <typename Resource, typename T>
    void updateManaged(Resource &resource, const T &value, bool force)
    {
        if (force || !(resource.getValue() == value))
            resource.setValue(value);
    }

    bool parseHexCode(const String &text, uint32_t &code)
    {
        String candidate = text;
        candidate.trim();
        if (candidate.startsWith("0x") || candidate.startsWith("0X"))
            candidate.remove(0, 2);
        if (candidate.length() == 0 || candidate.length() > 8)
            return false;

        uint32_t parsed = 0;
        for (size_t i = 0; i < candidate.length(); ++i)
        {
            const char c = candidate[i];
            uint8_t digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<uint8_t>(c - 'A' + 10);
            else
                return false;
            parsed = (parsed << 4) | digit;
        }
        code = parsed;
        return true;
    }

    ActionResult invokeIrSend(ManagedAction &, const String &payload)
    {
        DynamicJsonDocument doc(192);
        if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
            return {false, "expected {\"code\":\"POWER\"}"};

        JsonVariantConst value = doc["code"];
        if (!value.is<const char *>())
            return {false, "'code' must be a string"};

        const String requested = value.as<String>();
        uint32_t code = getIrCode(requested);
        if (code == 0 && (!parseHexCode(requested, code) || getIrProtocol(code) == IR_PROTO_NONE))
            return {false, "unknown IR code; use a known name or hexadecimal value"};
        if (!sendIRCode(code))
            return {false, "IR transmit queue is full"};

        return {true, String("queued ") + getIrName(code)};
    }

    ActionResult invokeAcManualSync(ManagedAction &, const String &payload)
    {
        DynamicJsonDocument doc(192);
        if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
        {
            // we try to get payload as human-readable string: <power> <temperature>
            String powerStr;
            String temperatureStr;
            int spaceIndex = payload.indexOf(' ');
            int temp = 0;
            if (spaceIndex >= 0)
            {
                powerStr = payload.substring(0, spaceIndex);
                temperatureStr = payload.substring(spaceIndex + 1);
                temp = temperatureStr.toInt();
            }
            bool isOk = powerStr.length() > 0 && temperatureStr.length() > 0 && spaceIndex > 0 && temp >0;
            if (!isOk)
                return {false, "deserializeJson failed or payload is not a JSON object; expected {\"power\":bool,\"temperature\":int} got: " + payload};
            doc = DynamicJsonDocument(192);
            doc["power"] = powerStr == "1" || powerStr.equalsIgnoreCase("true");
            doc["temperature"] = temp;
        }

        JsonVariantConst power = doc["power"];
        JsonVariantConst temperature = doc["temperature"];
        if (!power.is<bool>() || !temperature.is<int>())
            return {false, formatString("'power' (%s) must be boolean and 'temperature' (%s) must be an integer", power.as<String>().c_str(), temperature.as<String>().c_str())};
        if (!gAc.manualSync(power.as<bool>(), temperature.as<int>()))
            return {false, "temperature must be between 18 and 30"};

        syncAcResources();
        return {true, gAc.stateJson()};
    }

    ActionResult invokeAcSleep(ManagedAction &, const String &payload)
    {
        DynamicJsonDocument doc(128);
        if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
            return {false, "expected {\"minutes\":30}"};

        JsonVariantConst minutes = doc["minutes"];
        if (!minutes.is<uint32_t>() || !gAc.setSleepMinutes(minutes.as<uint32_t>()))
            return {false, "'minutes' must be a non-negative integer in range"};

        syncAcResources();
        return {true, gAc.stateJson()};
    }

    ActionResult invokeAcMorningOff(ManagedAction &, const String &payload)
    {
        bool force = false;
        if (!payload.isEmpty())
        {
            DynamicJsonDocument doc(96);
            if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
                return {false, "expected an empty payload or {\"force\":true}"};
            JsonVariantConst requested = doc["force"];
            if (!requested.isNull() && !requested.is<bool>())
                return {false, "'force' must be boolean"};
            force = requested.as<bool>();
        }

        gAc.morningOff(force);
        syncAcResources();
        return {true, gAc.stateJson()};
    }

    bool writeAcPower(ManagedState<bool> &, const bool &requested)
    {
        return gAc.setPower(requested);
    }

    bool writeAcTemperature(ManagedState<uint8_t> &, const uint8_t &requested)
    {
        return gAc.setUnitTemperature(requested);
    }

    bool writeAcTarget(ManagedState<float> &, const float &requested)
    {
        return gAc.setTarget(requested);
    }

    bool writeAcSleepIn(ManagedState<bool> &, const bool &requested)
    {
        gAc.setSleepIn(requested);
        return true;
    }

    bool writeAcDoorPaused(ManagedState<bool> &, const bool &requested)
    {
        gAc.setDoorPaused(requested);
        return true;
    }

    bool writeAcMode(ManagedState<int32_t> &, const int32_t &requested)
    {
        if (requested < static_cast<int32_t>(AC_MODE_COOL) ||
            requested > static_cast<int32_t>(AC_MODE_HUMIDIFIER))
            return false;
        return gAc.setMode(static_cast<AcIrMode>(requested));
    }

    bool writeAcFan(ManagedState<uint8_t> &, const uint8_t &requested)
    {
        return gAc.setFan(requested);
    }

    bool writeAcTurbo(ManagedState<bool> &, const bool &requested)
    {
        return gAc.setTurbo(requested);
    }

    bool writeAcLed(ManagedState<bool> &, const bool &requested)
    {
        return gAc.setLed(requested);
    }

    bool bind(NetResource &resource)
    {
        return resource.isBound() || gResourcesManager.bindResource(&resource);
    }
}

#if BOARD_HAS_DS18B20
ManagedSensor<float> temperatureSensor("temperature");
#else
const ActionArgMetadata kSetTemperatureSensorArguments[] = {
    {"owner", NetValueType::STRING},
    {"name", NetValueType::STRING},
};
ManagedAction setTemperatureExternalSensor("set_temperature_sensor", kSetTemperatureSensorArguments);
RemoteSensor<float> temperatureSensor;
ActionResult handleSetTemperatureSensor(ManagedAction &, const String &payload)
{
    DynamicJsonDocument doc(192);
    if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
    {
        LOG("NetResources", "set_temperature_sensor: expected {\"owner\":\"device\",\"name\":\"resource\"}");
        return {false, "expected {\"owner\":\"device\",\"name\":\"resource\"}"};
    }

    JsonVariantConst ownerValue = doc["owner"];
    JsonVariantConst nameValue = doc["name"];
    if (!ownerValue.is<const char *>() || !nameValue.is<const char *>())
    {
        LOG("NetResources", "set_temperature_sensor: 'owner' and 'name' must be strings");
        return {false, "'owner' and 'name' must be strings"};
    }

    const String owner = ownerValue.as<String>();
    const String name = nameValue.as<String>();
    PersistentSettings.set("temperature_sensor_owner", owner);
    PersistentSettings.set("temperature_sensor_name", name);
    temperatureSensor.setSource(owner, name);
    return {true, String("configured temperature sensor: ") + owner + "/" + name};
}

#endif
#if BOARD_HAS_IR_RECEIVER
ManagedSensor<String> irRecvSensor("irrecv");
#endif
ManagedAction irSendAction("irsend", kIrSendArguments);
RemoteSensor<bool> doorSensor;
ManagedSensor<String> acStatus("ac_status");
ManagedSensor<int8_t> acControllerState("ac_state");
ManagedSensor<bool> acKnown("ac_known");
ManagedSensor<uint8_t> acDoorState("ac_door_state");
ManagedSensor<uint32_t> acSleepDeadline("ac_sleep_deadline");
ManagedState<bool> acPower("ac_power");
ManagedState<uint8_t> acUnitTemperature("ac_temperature");
ManagedState<float> acTarget("ac_target");
ManagedState<bool> acSleepIn("ac_sleep_in");
ManagedState<bool> acDoorPaused("ac_door_paused");
ManagedState<int32_t> acMode("ac_mode");
ManagedState<uint8_t> acFan("ac_fan");
ManagedState<bool> acTurbo("ac_turbo");
ManagedState<bool> acLed("ac_led");
ManagedAction acManualSyncAction("ac_manual_sync", kAcManualSyncArguments);
ManagedAction acSleepAction("ac_sleep", kAcSleepArguments);
ManagedAction acMorningOffAction("ac_morning_off", kAcMorningOffArguments);
#if defined(BOARD_C6_V1)
ManagedState<uint32_t> rgbLedColor("rgb");
bool writeRgbLedColor(ManagedState<uint32_t> &resource, const uint32_t &requested)
{
    writeLedColor(requested);
    return true;
}
#endif

void syncAcResources()
{
    static bool initialized = false;
    const bool force = !initialized;
    updateManaged(acStatus, gAc.stateJson(), force);
    updateManaged(acControllerState, static_cast<int8_t>(gAc.state()), force);
    updateManaged(acKnown, acIrState.known, force);
    updateManaged(acDoorState, gAc.doorState(), force);
    updateManaged(acSleepDeadline, gAc.sleepDeadline(), force);
    updateManaged(acPower, acIrState.state.power, force);
    updateManaged(acUnitTemperature, acIrState.state.temp, force);
    updateManaged(acTarget, gAc.target(), force);
    updateManaged(acSleepIn, gAc.sleepIn(), force);
    updateManaged(acDoorPaused, gAc.doorPaused(), force);
    updateManaged(acMode, static_cast<int32_t>(acIrState.state.mode), force);
    updateManaged(acFan, acIrState.state.fan, force);
    updateManaged(acTurbo, acIrState.state.turbo, force);
    updateManaged(acLed, acIrState.state.led, force);
    initialized = true;
}

bool bindResources()
{
    irSendAction.onInvoke = invokeIrSend;
    acManualSyncAction.onInvoke = invokeAcManualSync;
    acSleepAction.onInvoke = invokeAcSleep;
    acMorningOffAction.onInvoke = invokeAcMorningOff;
    acPower.onWrite = writeAcPower;
    acUnitTemperature.onWrite = writeAcTemperature;
    acTarget.onWrite = writeAcTarget;
    acSleepIn.onWrite = writeAcSleepIn;
    acDoorPaused.onWrite = writeAcDoorPaused;
    acMode.onWrite = writeAcMode;
    acFan.onWrite = writeAcFan;
    acTurbo.onWrite = writeAcTurbo;
    acLed.onWrite = writeAcLed;
    // Keep the source configurable without reviving the old NetworkSensor
    // abstraction. RemoteSensor owns subscription, decoding and freshness.
    if (!doorSensor.isBound())
    {
        const String doorDevice = PersistentSettings.get("door_device", "");
        const String doorResource = PersistentSettings.get(
            "door_key", PersistentSettings.get("door_resource", "door"));
        doorSensor.setSource(doorDevice, doorResource);
    }

    bool ok = true;
#if BOARD_HAS_DS18B20
    ok = bind(temperatureSensor) && ok;
#else
    setTemperatureExternalSensor.onInvoke = handleSetTemperatureSensor;
    const String tempSensorOwner = PersistentSettings.get("temperature_sensor_owner", "");
    const String tempSensorResource = PersistentSettings.get(
        "temperature_sensor_name", PersistentSettings.get("temperature_sensor_resource", ""));
    if (!tempSensorOwner.isEmpty() && !tempSensorResource.isEmpty())
        temperatureSensor.setSource(tempSensorOwner, tempSensorResource);
    ok = bind(setTemperatureExternalSensor) && ok;
    ok = bind(temperatureSensor) && ok;
#endif
#if BOARD_HAS_IR_RECEIVER
    ok = bind(irRecvSensor) && ok;
#endif
#if defined(BOARD_C6_V1)
    rgbLedColor.onWrite = writeRgbLedColor;
    ok = bind(rgbLedColor) && ok;

#endif
    ok = bind(irSendAction) && ok;
    ok = bind(doorSensor) && ok;
    ok = bind(acStatus) && ok;
    ok = bind(acControllerState) && ok;
    ok = bind(acKnown) && ok;
    ok = bind(acDoorState) && ok;
    ok = bind(acSleepDeadline) && ok;
    ok = bind(acPower) && ok;
    ok = bind(acUnitTemperature) && ok;
    ok = bind(acTarget) && ok;
    ok = bind(acSleepIn) && ok;
    ok = bind(acDoorPaused) && ok;
    ok = bind(acMode) && ok;
    ok = bind(acFan) && ok;
    ok = bind(acTurbo) && ok;
    ok = bind(acLed) && ok;
    ok = bind(acManualSyncAction) && ok;
    ok = bind(acSleepAction) && ok;
    ok = bind(acMorningOffAction) && ok;
    return ok;
}
