#include "AcResources.h"

#include <ArduinoJson.h>
#include <NightMare.h>
#include <IrController/IrController.h>

static String formatArgs(const char *format, ...)
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

    ActionResult invokeAcManualSync(ManagedAction &, const String &payload)
    {
        JsonDocument doc;
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
            bool isOk = powerStr.length() > 0 && temperatureStr.length() > 0 && spaceIndex > 0 && temp > 0;
            if (!isOk)
                return {false, "deserializeJson failed or payload is not a JSON object; expected {\"power\":bool,\"temperature\":int} got: " + payload};
            doc.clear();
            doc["power"] = powerStr == "1" || powerStr.equalsIgnoreCase("true");
            doc["temperature"] = temp;
        }

        JsonVariantConst power = doc["power"];
        JsonVariantConst temperature = doc["temperature"];
        if (!power.is<bool>() || !temperature.is<int>())
            return {false, formatArgs("'power' (%s) must be boolean and 'temperature' (%s) must be an integer", power.as<String>().c_str(), temperature.as<String>().c_str())};
        if (!gAc.manualSync(power.as<bool>(), temperature.as<int>()))
            return {false, "temperature must be between 18 and 30"};

        gAc.publishResources();
        return {true, gAc.stateJson()};
    }

    ActionResult invokeAcSleep(ManagedAction &, const String &payload)
    {
        JsonDocument doc;
        if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
            return {false, "expected {\"minutes\":30}"};

        JsonVariantConst minutes = doc["minutes"];
        if (!minutes.is<uint32_t>() || !gAc.setSleepMinutes(minutes.as<uint32_t>()))
            return {false, "'minutes' must be a non-negative integer in range"};

        gAc.publishResources();
        return {true, gAc.stateJson()};
    }

    ActionResult invokeAcMorningOff(ManagedAction &, const String &payload)
    {
        bool force = false;
        if (!payload.isEmpty())
        {
            JsonDocument doc;
            if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
                return {false, "expected an empty payload or {\"force\":true}"};
            JsonVariantConst requested = doc["force"];
            if (!requested.isNull() && !requested.is<bool>())
                return {false, "'force' must be boolean"};
            force = requested.as<bool>();
        }

        gAc.morningOff(force);
        gAc.publishResources();
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

}

// ---- declarations: stable names are the contract Adler exposes ----------------------

RemoteSensor<bool> doorSensor("door");
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

void AcController::publishResources()
{
    static bool initialized = false;
    const bool force = !initialized;
    updateManaged(acStatus, stateJson(), force);
    updateManaged(acControllerState, static_cast<int8_t>(state()), force);
    updateManaged(acKnown, acIrState.known, force);
    updateManaged(acDoorState, doorState(), force);
    updateManaged(acSleepDeadline, sleepDeadline(), force);
    updateManaged(acPower, acIrState.state.power, force);
    updateManaged(acUnitTemperature, acIrState.state.temp, force);
    updateManaged(acTarget, target(), force);
    updateManaged(acSleepIn, sleepIn(), force);
    updateManaged(acDoorPaused, doorPaused(), force);
    updateManaged(acMode, static_cast<int32_t>(acIrState.state.mode), force);
    updateManaged(acFan, acIrState.state.fan, force);
    updateManaged(acTurbo, acIrState.state.turbo, force);
    updateManaged(acLed, acIrState.state.led, force);
    initialized = true;
}

void installAcResourceHandlers()
{
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
}
