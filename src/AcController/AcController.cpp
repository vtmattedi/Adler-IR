#include "AcController.h"

#include "AcResources.h"
#include <math.h>

namespace
{
constexpr char MorningOffJob[] = "ac_morning_off";
constexpr char SleepInOffJob[] = "ac_sleep_in_off";
constexpr uint32_t MaxSleepMinutes = INT32_MAX / 60000UL;

bool parseOnOff(const String &text, bool &value)
{
    String normalized = text;
    normalized.trim();
    normalized.toUpperCase();
    if (normalized == "1" || normalized == "ON" || normalized == "TRUE")
        value = true;
    else if (normalized == "0" || normalized == "OFF" || normalized == "FALSE")
        value = false;
    else
        return false;
    return true;
}

}

AcController gAc;

void AcController::begin()
{
    installAcConfigPolicy();
    syncConfig();
    target_ = -fabsf(defaultTarget_);
    installAcResourceHandlers();
}

void AcController::syncConfig()
{
    hysteresis_ = acHysteresisConfig.value();
    inputStaleMs_ = acInputStaleSecondsConfig.value() * 1000UL;
    doorSecsToPause_ = acDoorPauseSecondsConfig.value();
    doorSecsToStop_ = acDoorStopSecondsConfig.value();
    doorControlEnabled_ = acDoorControlConfig.value();
    doorInvert_ = doorInvertConfig.value();
    const float configuredDefaultTarget = acDefaultTargetConfig.value();
    const bool targetFollowsDefault = target_ < 0.0f &&
                                      fabsf(target_) == defaultTarget_;
    defaultTarget_ = configuredDefaultTarget;
    if (targetFollowsDefault)
        target_ = -defaultTarget_;

    const TimeType &turnOffTime = acTurnOffTimeConfig.value();
    const TimeType &sleepInTurnOffTime = acSleepInTurnOffTimeConfig.value();
    if (turnOffTime_ != turnOffTime || sleepInTurnOffTime_ != sleepInTurnOffTime)
    {
        LOG("AC", "Shutdown times changed to %s / %s: rescheduling", turnOffTime.toString().c_str(),
            sleepInTurnOffTime.toString().c_str());
        turnOffTime_ = turnOffTime;
        sleepInTurnOffTime_ = sleepInTurnOffTime;
        gScheduler.remove(MorningOffJob);
        gScheduler.remove(SleepInOffJob);
        tasksEnsured_ = false;
    }
}

void AcController::loop()
{
    syncConfig();
    const uint32_t nowMs = millis();
    if (lastLoopMs_ != 0 && nowMs - lastLoopMs_ < AC_LOOP_INTERVAL_MS)
        return;
    lastLoopMs_ = nowMs;

    doorLoop();
    sleepLoop();
    targetLoop();
    ensureScheduledTasks();
}

bool AcController::currentTemperature(float &temperature) const
{
#if BOARD_HAS_DS18B20
    const TempSensorStatus status = tempSensorStatus();
    if (!status.connected || status.lastReadMs == 0 ||
        millis() - status.lastReadMs > inputStaleMs_ || isnan(status.tempC))
        return false;
    temperature = status.tempC;
#else
    if (!externalTemperatureSensor.hasValue() || externalTemperatureSensor.isStale())
        return false;
    temperature = temperatureSensor.getValue();
#endif
    return !isnan(temperature);
}

void AcController::doorLoop()
{
    if (!doorControlEnabled_ || doorPausedByUser_ || !doorSensor.hasValue() || doorSensor.isStale())
        return;

    const bool open = doorSensor.getValue() != doorInvert_;
    const uint32_t nowMs = millis();
    if (open)
    {
        if (!doorOpen_)
        {
            doorOpen_ = true;
            doorOpenMs_ = nowMs;
            doorOpenEpoch_ = NightMare::Time::valid() ? NightMare::Time::now() : 0;
        }
        const uint32_t openFor = (nowMs - doorOpenMs_) / 1000UL;
        if (!pausedByDoor_ && openFor >= doorSecsToPause_)
        {
            resumePowerAfterDoor_ = acIrState.state.power;
            if (resumePowerAfterDoor_)
                acIrState.setPower(false);
            pausedByDoor_ = true;
            LOG("AC", "Door open %lu s: paused", (unsigned long)openFor);
        }
        if (pausedByDoor_ && !stoppedByDoor_ && openFor >= doorSecsToStop_)
        {
            if (target_ > 0)
                target_ = -target_;
            resumePowerAfterDoor_ = false;
            stoppedByDoor_ = true;
            LOG("AC", "Door open %lu s: target disabled", (unsigned long)openFor);
        }
        return;
    }

    if (!doorOpen_)
        return;
    LOG("AC", "Door closed");
    doorOpen_ = false;
    doorOpenMs_ = 0;
    doorOpenEpoch_ = 0;
    if (pausedByDoor_ && !stoppedByDoor_ && resumePowerAfterDoor_)
        acIrState.setPower(true);
    pausedByDoor_ = false;
    stoppedByDoor_ = false;
    resumePowerAfterDoor_ = false;
}

void AcController::sleepLoop()
{
    if (!sleepArmed_ || static_cast<int32_t>(millis() - sleepDeadlineMs_) < 0)
        return;
    LOG("AC", "Sleep timer elapsed: off");
    sleepArmed_ = false;
    sleepDeadlineMs_ = 0;
    sleepDeadlineEpoch_ = 0;
    if (target_ > 0)
        target_ = -target_;
    acIrState.setPower(false);
}

void AcController::targetLoop()
{
    if (target_ < 0 || pausedByDoor_)
        return;
    float current = NAN;
    if (!currentTemperature(current))
        return;
    const float delta = current - target_;
    if (delta > hysteresis_)
        acIrState.setPower(true);
    else if (delta < -hysteresis_)
        acIrState.setPower(false);
}

bool AcController::scheduleMorningOff(bool forced)
{
    String clock = (forced ? sleepInTurnOffTime_ : turnOffTime_).toString();
    clock.remove(5); // NightMare's wall-clock scheduler is minute-resolution.
    const time_t due = NightMare::Time::timestampOfNextOccurrence(clock);
    if (due == 0)
    {
        LOG_ERROR("AC", "Invalid shutdown time: %s", clock.c_str());
        return false;
    }
    const int32_t id = gScheduler.atWall(forced ? SleepInOffJob : MorningOffJob,
                                         forced ? sleepInOffJob : morningOffJob,
                                         static_cast<uint32_t>(due));
    // Names the clock text it was built from and when it will really fire, so a shutdown at an
    // unexpected hour can be traced to the Config value or to the clock/timezone.
    LOG("AC", "%s shutdown job %s: configured %s, due %s (epoch %lu)", forced ? "Sleep-in" : "Morning",
        id >= 0 ? "scheduled" : "REFUSED", clock.c_str(),
        NightMare::Time::timestampToDateString(due).c_str(), (unsigned long)due);
    return id >= 0;
}

void AcController::ensureScheduledTasks()
{
    if (tasksEnsured_ || !NightMare::Time::valid() ||
        !SystemState.get(SystemFlag::TimeSynced))
        return;
    gScheduler.remove(MorningOffJob);
    gScheduler.remove(SleepInOffJob);
    const bool morning = scheduleMorningOff(false);
    const bool sleepIn = scheduleMorningOff(true);
    tasksEnsured_ = morning && sleepIn;
    if (!tasksEnsured_)
    {
        gScheduler.remove(MorningOffJob);
        gScheduler.remove(SleepInOffJob);
        return;
    }
    const String turnOffTime = turnOffTime_.toString();
    const String sleepInTurnOffTime = sleepInTurnOffTime_.toString();
    LOG("AC", "Shutdown scheduled at %s (sleep-in %s)", turnOffTime.c_str(),
        sleepInTurnOffTime.c_str());
}

void AcController::morningOffJob()
{
    gAc.morningOff(false);
    if (!gAc.scheduleMorningOff(false))
        gAc.tasksEnsured_ = false;
}

void AcController::sleepInOffJob()
{
    gAc.morningOff(true);
    if (!gAc.scheduleMorningOff(true))
        gAc.tasksEnsured_ = false;
}

bool AcController::setUnitTemperature(uint8_t temperature)
{
    return acIrState.setTemp(temperature);
}

bool AcController::setTarget(float target)
{
    if (target >= 0 && (target < MIN_AC_TEMP || target > MAX_AC_TEMP))
        return false;
    target_ = target < 0 ? -fabsf(target) : target;
    stoppedByDoor_ = false;
    return true;
}

bool AcController::setPower(bool on)
{
    const bool accepted = acIrState.setPower(on);
    if (accepted)
    {
        pausedByDoor_ = false;
        stoppedByDoor_ = false;
    }
    return accepted;
}

bool AcController::setMode(AcIrMode mode)
{
    return acIrState.setMode(mode);
}

bool AcController::setFan(uint8_t fan)
{
    if (fan > 1)
        return false;
    if (fan == acIrState.state.fan)
        return true;
    return acIrState.state.power && sendIRCode(AC_VENTILATOR, IR_SRC_AC_CONTROLLER);
}

bool AcController::setTurbo(bool enabled)
{
    if (enabled == acIrState.state.turbo)
        return true;
    return acIrState.state.power && sendIRCode(AC_TURBO, IR_SRC_AC_CONTROLLER);
}

bool AcController::setLed(bool enabled)
{
    if (enabled == acIrState.state.led)
        return true;
    return acIrState.state.power && sendIRCode(AC_LED, IR_SRC_AC_CONTROLLER);
}

bool AcController::manualSync(bool on, uint8_t temperature)
{
    if (temperature < MIN_AC_TEMP || temperature > MAX_AC_TEMP)
        return false;
    acIrState.manualSync(on, temperature);
    return true;
}

void AcController::setSleepIn(bool armed)
{
    sleepIn_ = armed;
}

void AcController::setDoorPaused(bool paused)
{
    doorPausedByUser_ = paused;
    if (paused && pausedByDoor_)
    {
        if (resumePowerAfterDoor_)
            acIrState.setPower(true);
        pausedByDoor_ = false;
        stoppedByDoor_ = false;
        resumePowerAfterDoor_ = false;
    }
}

bool AcController::setSleepMinutes(uint32_t minutes)
{
    if (minutes > MaxSleepMinutes)
        return false;
    if (minutes == 0)
    {
        sleepArmed_ = false;
        sleepDeadlineMs_ = 0;
        sleepDeadlineEpoch_ = 0;
        return true;
    }
    sleepArmed_ = true;
    sleepDeadlineMs_ = millis() + minutes * 60000UL;
    sleepDeadlineEpoch_ = NightMare::Time::valid()
                              ? static_cast<uint32_t>(NightMare::Time::now()) + minutes * 60UL
                              : 0;
    return true;
}

/// Shutdown decided by the unit's real state, not by what we last queued. The IR belief
/// can be wrong (a missed or manual remote press); a POWER toggle on the wrong belief
/// would turn an off unit on, or leave a running one running.
void AcController::shutdownUnit()
{
    const AcPowerGuess guess = guessAcPower();
    if (guess == AcPowerGuess::UNKNOWN)
    {
        acIrState.setPower(false); // nothing better than the belief
        return;
    }
    const bool on = guess == AcPowerGuess::ON;
    if (acIrState.state.power != on)
    {
        LOG("AC", "IR belief (%s) disagrees with unit (%s): trusting the unit",
            acIrState.state.power ? "on" : "off", on ? "on" : "off");
        acIrState.manualSync(on, acIrState.state.temp);
    }
    acIrState.setPower(false); // sends POWER only if the (now corrected) belief is on
}

void AcController::morningOff(bool force)
{
    LOG("AC", "Shutdown fired at %s (forced %d; Config ac_turn_off_time=%s, sleep-in=%s)",
        NightMare::Time::fullTimeString().c_str(), force, acTurnOffTimeConfig.value().toString().c_str(),
        acSleepInTurnOffTimeConfig.value().toString().c_str());
    if (!force && sleepIn_)
    {
        LOG("AC", "Morning shutdown skipped: sleep-in armed");
        return;
    }
    sleepIn_ = false;
    if (target_ > 0)
        target_ = -target_;
    shutdownUnit();
    LOG("AC", "Morning shutdown");
}

AcControllerState AcController::state() const
{
    if (!acIrState.known)
        return AC_UNKNOWN;
    const bool targeting = target_ > 0;
    if (acIrState.state.power)
        return targeting ? AC_ON_TARGET : AC_ON;
    if (pausedByDoor_)
        return targeting ? AC_OFF_TARGET_DOOR_OPEN : AC_OFF_DOOR_OPEN;
    return targeting ? AC_OFF_TARGET : AC_OFF;
}

uint8_t AcController::doorState() const
{
    uint8_t state = 0;
    if (doorOpen_)
        state |= 1 << 0;
    if (doorPausedByUser_)
        state |= 1 << 1;
    if (pausedByDoor_)
        state |= 1 << 2;
    return state;
}

String AcController::stateJson() const
{
    JsonDocument doc;
    const int temperature = acIrState.state.temp;
    doc["AcState"] = static_cast<int>(state());
    doc["DoorState"] = doorState();
    doc["Temp"] = acIrState.state.power ? temperature : -temperature;
    doc["Settemp"] = target_;
    float current = NAN;
    if (currentTemperature(current))
        doc["CurrTemp"] = current;
    else
        doc["CurrTemp"] = nullptr;
    doc["Hsleep"] = 0;
    doc["Ssleep"] = sleepDeadlineEpoch_;
    doc["Door"] = doorOpenEpoch_;
    doc["SleepIn"] = sleepIn_;
    doc["known"] = acIrState.known;
    doc["mode"] = static_cast<int>(acIrState.state.mode);
    doc["fan"] = acIrState.state.fan;
    doc["turbo"] = acIrState.state.turbo;
    doc["led"] = acIrState.state.led;
    String output;
    serializeJson(doc, output);
    return output;
}

void AcController::info(JsonObject into) const
{
    into["state"] = static_cast<int>(state());
    into["target"] = target_;
    into["sleepIn"] = sleepIn_;
    into["sleepDeadline"] = sleepDeadlineEpoch_;
    JsonObject config = into["config"].to<JsonObject>();
    config["hysteresis"] = hysteresis_;
    config["inputStaleMs"] = inputStaleMs_;
    config["doorPauseSeconds"] = doorSecsToPause_;
    config["doorStopSeconds"] = doorSecsToStop_;
    config["doorControl"] = doorControlEnabled_;
    config["doorInvert"] = doorInvert_;
    config["turnOffTime"] = turnOffTime_.toString();
    config["sleepInTurnOffTime"] = sleepInTurnOffTime_.toString();
}

bool AcController::handles(const String &command) const
{
    return command == "AC" || command == "SETTEMP" || command == "TARGET" ||
           command == "POWER" || command == "MANUALSYNC" || command == "SLEEP-IN" ||
           command == "PAUSEDOORSENSOR" || command == "SLEEP";
}

NightMareResults AcController::command(const NightMareMessage &message)
{
    NightMareResults result;
    result.result = false;
    auto success = [&](const String &response) {
        result.result = true;
        result.response = response;
        return result;
    };
    auto failure = [&](const String &response) {
        result.response = response;
        return result;
    };

    if (message.command == "SETTEMP")
    {
        const int temperature = message.args[0].toInt();
        return setUnitTemperature(temperature) ? success(stateJson())
                                               : failure("SETTEMP requires 18-30");
    }
    if (message.command == "TARGET")
    {
        if (message.args[0].length() == 0)
            return failure("Usage: TARGET <18-30|-1>");
        return setTarget(message.args[0].toFloat()) ? success(stateJson())
                                                    : failure("TARGET requires 18-30 or a negative value");
    }
    if (message.command == "POWER")
    {
        bool on = false;
        if (!parseOnOff(message.args[0], on))
            return failure("Usage: POWER <0|1>");
        return setPower(on) ? success(stateJson()) : failure("IR queue is full");
    }
    if (message.command == "MANUALSYNC")
    {
        bool on = false;
        const int temperature = message.args[1].toInt();
        if (!parseOnOff(message.args[0], on) || !manualSync(on, temperature))
            return failure("Usage: MANUALSYNC <0|1> <18-30>");
        return success(stateJson());
    }
    if (message.command == "SLEEP-IN")
    {
        bool armed = false;
        if (!parseOnOff(message.args[0], armed))
            return failure("Usage: SLEEP-IN <0|1>");
        setSleepIn(armed);
        return success(stateJson());
    }
    if (message.command == "PAUSEDOORSENSOR")
    {
        bool paused = false;
        if (!parseOnOff(message.args[0], paused))
            return failure("Usage: PAUSEDOORSENSOR <0|1>");
        setDoorPaused(paused);
        return success(stateJson());
    }
    if (message.command == "SLEEP")
    {
        if (message.args[0].length() == 0 || !setSleepMinutes(message.args[0].toInt()))
            return failure("Usage: SLEEP <minutes|0>");
        return success(stateJson());
    }
    if (message.command != "AC")
        return failure("Not an AC command");

    if (message.subcommand.length() == 0 || message.subcommand == "STATE")
        return success(stateJson());
    if (message.subcommand == "INFO")
    {
        JsonDocument doc;
        info(doc.to<JsonObject>());
        String output;
        serializeJson(doc, output);
        return success(output);
    }
    if (message.subcommand == "MORNINGOFF")
    {
        String option = message.args[1];
        option.toUpperCase();
        morningOff(option == "FORCE");
        return success(stateJson());
    }
    if (message.subcommand == "HELP")
        return success("AC [STATE|INFO|MORNINGOFF [FORCE]]. Root: SETTEMP, TARGET, POWER, MANUALSYNC, SLEEP-IN, PAUSEDOORSENSOR, SLEEP");
    return failure("Unknown AC subcommand: STATE, INFO, MORNINGOFF, HELP");
}
