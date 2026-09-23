#include "AcController.h"

#include <math.h>
#if BOARD_HAS_DS18B20
#include <TempSensor/TempSensor.h>
#endif

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

void ensureSetting(const String &key, const String &value)
{
    if (!PersistentSettings.exists(key))
        PersistentSettings.set(key, value);
}
}

AcController gAc;

#if BOARD_HAS_DS18B20
void AcController::begin(ManagedSensor<float> &temperature, RemoteSensor<bool> &door)
#else
void AcController::begin(RemoteSensor<float> &temperature, RemoteSensor<bool> &door)
#endif
{
    temperature_ = &temperature;
    door_ = &door;
    loadConfig();
    target_ = -fabsf(defaultTarget_);
}

void AcController::loadConfig()
{
    ensureSetting("ac_hysteresis", String(AC_DEFAULT_HYSTERESIS));
    ensureSetting("ac_input_stale_secs", String(AC_DEFAULT_INPUT_STALE_SECONDS));
    ensureSetting("ac_door_pause_secs", "300");
    ensureSetting("ac_door_stop_secs", "600");
    ensureSetting("ac_door_control", "1");
    ensureSetting("ac_turn_off_time", "05:30");
    ensureSetting("ac_sleep_in_turn_off_time", "08:30");
    ensureSetting("ac_default_target", "24");

    const float hysteresis = PersistentSettings.get("ac_hysteresis", "0.5").toFloat();
    hysteresis_ = hysteresis > 0.0f ? hysteresis : AC_DEFAULT_HYSTERESIS;

    uint32_t staleSeconds = PersistentSettings.get(
        "ac_input_stale_secs", String(AC_DEFAULT_INPUT_STALE_SECONDS)).toInt();
    if (staleSeconds == 0 || staleSeconds > INT32_MAX / 1000UL)
        staleSeconds = AC_DEFAULT_INPUT_STALE_SECONDS;
    inputStaleMs_ = staleSeconds * 1000UL;

    doorSecsToPause_ = PersistentSettings.get("ac_door_pause_secs", "300").toInt();
    doorSecsToStop_ = PersistentSettings.get("ac_door_stop_secs", "600").toInt();
    if (doorSecsToStop_ < doorSecsToPause_)
        doorSecsToStop_ = doorSecsToPause_;
    doorControlEnabled_ = PersistentSettings.get("ac_door_control", "1") != "0";
    doorInvert_ = PersistentSettings.get("door_invert", "0") == "1";
    turnOffTime_ = PersistentSettings.get("ac_turn_off_time", "05:30");
    sleepInTurnOffTime_ = PersistentSettings.get("ac_sleep_in_turn_off_time", "08:30");

    const float configuredTarget = PersistentSettings.get("ac_default_target", "24").toFloat();
    defaultTarget_ = configuredTarget >= MIN_AC_TEMP && configuredTarget <= MAX_AC_TEMP
                         ? configuredTarget
                         : 24.0f;
}

void AcController::reloadConfig()
{
    loadConfig();
    gScheduler.remove(MorningOffJob);
    gScheduler.remove(SleepInOffJob);
    tasksEnsured_ = false;
}

void AcController::loop()
{
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
    if (temperature_ == nullptr || !temperature_->hasValue() || temperature_->isStale())
        return false;
    temperature = temperature_->getValue();
#endif
    return !isnan(temperature);
}

void AcController::doorLoop()
{
    if (!doorControlEnabled_ || doorPausedByUser_ || door_ == nullptr ||
        !door_->hasValue() || door_->isStale())
        return;

    const bool open = door_->getValue() != doorInvert_;
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
    const String &clock = forced ? sleepInTurnOffTime_ : turnOffTime_;
    const time_t due = NightMare::Time::timestampOfNextOccurrence(clock);
    if (due == 0)
    {
        LOG_ERROR("AC", "Invalid shutdown time: %s", clock.c_str());
        return false;
    }
    const int32_t id = gScheduler.atWall(forced ? SleepInOffJob : MorningOffJob,
                                         forced ? sleepInOffJob : morningOffJob,
                                         static_cast<uint32_t>(due));
    return id >= 0;
}

void AcController::ensureScheduledTasks()
{
    if (tasksEnsured_ || !NightMare::Time::valid() || !SystemState.getFlag("time_synced"))
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
    LOG("AC", "Shutdown scheduled at %s (sleep-in %s)", turnOffTime_.c_str(),
        sleepInTurnOffTime_.c_str());
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
    if (target_ > 0)
        defaultTarget_ = target_;
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
    return acIrState.state.power && sendIRCode(AC_VENTILATOR);
}

bool AcController::setTurbo(bool enabled)
{
    if (enabled == acIrState.state.turbo)
        return true;
    return acIrState.state.power && sendIRCode(AC_TURBO);
}

bool AcController::setLed(bool enabled)
{
    if (enabled == acIrState.state.led)
        return true;
    return acIrState.state.power && sendIRCode(AC_LED);
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

void AcController::morningOff(bool force)
{
    if (!force && sleepIn_)
    {
        LOG("AC", "Morning shutdown skipped: sleep-in armed");
        return;
    }
    sleepIn_ = false;
    if (target_ > 0)
        target_ = -target_;
    acIrState.setPower(false);
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
    config["turnOffTime"] = turnOffTime_;
    config["sleepInTurnOffTime"] = sleepInTurnOffTime_;
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
    if (message.subcommand == "RELOAD")
    {
        reloadConfig();
        return success(stateJson());
    }
    if (message.subcommand == "MORNINGOFF")
    {
        String option = message.args[1];
        option.toUpperCase();
        morningOff(option == "FORCE");
        return success(stateJson());
    }
    if (message.subcommand == "HELP")
        return success("AC [STATE|INFO|RELOAD|MORNINGOFF [FORCE]]. Root: SETTEMP, TARGET, POWER, MANUALSYNC, SLEEP-IN, PAUSEDOORSENSOR, SLEEP");
    return failure("Unknown AC subcommand: STATE, INFO, RELOAD, MORNINGOFF, HELP");
}
