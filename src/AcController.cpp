#include "AcController.h"
#include <TimeLib.h>

AcController gAc;

// ---- config --------------------------------------------------------------------

void AcController::loadConfig()
{
    // Read with defaults, then write back, so a fresh unit's config file is complete and
    // every setting is visible and editable over CONFIG GET ALL.
    _hysteresis = Config.get("ac_hysteresis", String(DEFAULT_HYSTERESIS)).toDouble();
    _doorSecsToPause = Config.get("ac_door_pause_secs", "300").toInt();
    _doorSecsToStop = Config.get("ac_door_stop_secs", "600").toInt();
    _doorControlEnabled = Config.get("ac_door_control", "1") == "1";
    _turnOffTime = Config.get("ac_turn_off_time", "05:30");
    _sleepInTurnOffTime = Config.get("ac_sleep_in_turn_off_time", "08:30");
    _defaultTarget = Config.get("ac_default_target", "24").toDouble();
    Config.set("ac_hysteresis", String(_hysteresis));
    Config.set("ac_door_pause_secs", String(_doorSecsToPause));
    Config.set("ac_door_stop_secs", String(_doorSecsToStop));
    Config.set("ac_door_control", _doorControlEnabled ? "1" : "0");
    Config.set("ac_turn_off_time", _turnOffTime);
    Config.set("ac_sleep_in_turn_off_time", _sleepInTurnOffTime);
    Config.set("ac_default_target", String(_defaultTarget));
    Config.save();
    _configDirty = false;
}

void AcController::onConfigChanged(const String &key, const String &value)
{
    // Fires on whichever task ran CONFIG SET; only flag it, the loop reloads.
    if (key.startsWith("ac_"))
        gAc._configDirty = true;
}

void AcController::begin(float (*readTemp)(), NetworkSensor *door)
{
    _readTemp = readTemp;
    _door = door;
    loadConfig();
    _target = -fabs(_defaultTarget); // disabled until someone asks
    Config.NotifyOnChange(onConfigChanged);
}

// ---- the loop --------------------------------------------------------------------

void AcController::loop()
{
    if (_configDirty)
    {
        loadConfig();
        // Times may have changed: drop and recreate the scheduler tasks.
        scheduler.deleteTaskByLabel("ac_morning_off");
        scheduler.deleteTaskByLabel("ac_sleep_in_off");
        _tasksEnsured = false;
    }
    doorLoop();
    sleepLoop();
    targetLoop();
    ensureScheduledTasks();
    publishState();
}

void AcController::doorLoop()
{
    if (!_doorControlEnabled || _doorPausedByUser || !_door)
        return;

    bool open;
    NetworkSensorStatus s = _door->status();
    if (!s.bound || !s.fresh || !_door->asBool(open))
    {
        // Unknown is not closed. Whatever the door did before, nothing changes until it reports.
        return;
    }

    uint32_t t = now();
    if (open)
    {
        if (!_doorOpenTs)
            _doorOpenTs = t;
        uint32_t openFor = t - _doorOpenTs;

        if (!_pausedByDoor && openFor >= _doorSecsToPause)
        {
            // Soft pause: remember what to restore, turn the unit off.
            _resumePowerAfterDoor = acIrState.state.power;
            if (acIrState.state.power)
                acIrState.setPower(false);
            _pausedByDoor = true;
            Serial.printf("[ac] door open %lus: paused\n", (unsigned long)openFor);
        }
        if (_pausedByDoor && !_stoppedByDoor && openFor >= _doorSecsToStop)
        {
            // Hard stop: the room is not being kept; drop the target so nothing turns it back on.
            if (_target > 0)
                _target = -_target;
            _resumePowerAfterDoor = false;
            _stoppedByDoor = true;
            Serial.printf("[ac] door open %lus: stopped, target disabled\n", (unsigned long)openFor);
        }
    }
    else if (_doorOpenTs)
    {
        Serial.println("[ac] door closed");
        _doorOpenTs = 0;
        if (_pausedByDoor)
        {
            if (!_stoppedByDoor && _resumePowerAfterDoor)
                acIrState.setPower(true);
            _pausedByDoor = false;
            _stoppedByDoor = false;
            _resumePowerAfterDoor = false;
        }
    }
}

void AcController::sleepLoop()
{
    if (_swSleep && SystemSettings.getFlag("time_synced") && now() >= _swSleep)
    {
        Serial.println("[ac] sleep timer elapsed: off");
        _swSleep = 0;
        if (_target > 0)
            _target = -_target;
        acIrState.setPower(false);
    }
}

void AcController::targetLoop()
{
    if (_target < 0 || _pausedByDoor || !_readTemp)
        return;
    float current = _readTemp();
    if (isnan(current))
        return; // no thermometer, no opinion
    double delta = current - _target;
    if (delta > _hysteresis)
        acIrState.setPower(true);  // too warm
    else if (delta < -_hysteresis)
        acIrState.setPower(false); // cool enough
}

void AcController::ensureScheduledTasks()
{
    // Only with a synced clock: timestampOfNextOccurrence() on a 1970 clock produces a time
    // that the sync shift later moves to the wrong hour.
    if (_tasksEnsured || !SystemSettings.getFlag("time_synced"))
        return;
    if (!scheduler.taskExists("ac_morning_off"))
        scheduler.addTask("ac_morning_off", "AC MORNINGOFF", 86400, timestampOfNextOccurrence(_turnOffTime), true);
    if (!scheduler.taskExists("ac_sleep_in_off"))
        scheduler.addTask("ac_sleep_in_off", "AC MORNINGOFF FORCE", 86400, timestampOfNextOccurrence(_sleepInTurnOffTime), true);
    _tasksEnsured = true;
    Serial.printf("[ac] morning shutdown scheduled at %s (sleep-in: %s)\n", _turnOffTime.c_str(), _sleepInTurnOffTime.c_str());
}

// ---- the Service vocabulary --------------------------------------------------------

void AcController::setUnitTemperature(uint8_t temp)
{
    acIrState.setTemp(temp);
}

void AcController::setTarget(double target)
{
    _target = target;
    _stoppedByDoor = false;
    if (_target > 0)
        _defaultTarget = _target;
}

void AcController::setPower(bool on)
{
    acIrState.setPower(on);
    // A hand on the power is a hand on the pause: the door logic starts fresh.
    _pausedByDoor = false;
    _stoppedByDoor = false;
}

void AcController::manualSync(bool on, uint8_t temp)
{
    acIrState.manualSync(on, temp);
}

void AcController::setSleepIn(bool armed)
{
    _sleepIn = armed;
}

void AcController::setDoorPaused(bool paused)
{
    _doorPausedByUser = paused;
    if (paused && _pausedByDoor)
    {
        // Pausing the door logic while it holds the unit off releases it.
        if (_resumePowerAfterDoor)
            acIrState.setPower(true);
        _pausedByDoor = false;
        _stoppedByDoor = false;
    }
}

void AcController::setSleepMinutes(uint32_t minutes)
{
    _swSleep = minutes ? now() + minutes * 60 : 0;
}

void AcController::morningOff(bool force)
{
    if (!force && _sleepIn)
    {
        Serial.println("[ac] morning shutdown skipped: sleep-in armed");
        return;
    }
    _sleepIn = false;
    if (_target > 0)
        _target = -_target;
    if (acIrState.state.power)
        acIrState.setPower(false);
    Serial.println("[ac] morning shutdown");
}

// ---- reports ----------------------------------------------------------------------

AcState AcController::state() const
{
    if (!acIrState.known)
        return AC_UNKNOWN;
    bool on = acIrState.state.power;
    bool targeting = _target > 0;
    if (on)
        return targeting ? AC_ON_TARGET : AC_ON;
    if (_pausedByDoor)
        return targeting ? AC_OFF_TARGET_DOOR_OPEN : AC_OFF_DOOR_OPEN;
    return targeting ? AC_OFF_TARGET : AC_OFF;
}

uint8_t AcController::doorState() const
{
    uint8_t bits = 0;
    if (_doorOpenTs)
        bits |= 1 << 0;
    if (_doorPausedByUser)
        bits |= 1 << 1;
    if (_pausedByDoor)
        bits |= 1 << 2;
    return bits;
}

String AcController::stateJson() const
{
    DynamicJsonDocument doc(384);
    int temp = acIrState.state.temp;
    doc["AcState"] = (int)state();
    doc["DoorState"] = doorState();
    doc["Temp"] = acIrState.state.power ? temp : -temp;
    doc["Settemp"] = _target;
    float current = _readTemp ? _readTemp() : NAN;
    doc["CurrTemp"] = current; // null while unknown; the Service reads it as 0
    doc["Hsleep"] = 0;         // the unit's own timer is not observable from here
    doc["Ssleep"] = _swSleep;
    doc["Door"] = _doorOpenTs;
    doc["SleepIn"] = _sleepIn;
    String json;
    serializeJson(doc, json);
    return json;
}

void AcController::publishState(bool force)
{
    String json = stateJson();
    bool heartbeat = millis() - _lastPublishMs >= AC_STATE_HEARTBEAT_SECONDS * 1000UL;
    if (!force && !heartbeat && json == _lastStateJson)
        return;
    _lastStateJson = json;
    _lastPublishMs = millis();
    MQTT_Send("/state", json);
}

void AcController::info(JsonObject into) const
{
    JsonObject ac = into.createNestedObject("ac");
    ac["service"] = "AcController";
    ac["state"] = (int)state();
    ac["target"] = _target;
    ac["sleepIn"] = _sleepIn;
    ac["swSleep"] = _swSleep;
    JsonObject cfg = ac.createNestedObject("config");
    cfg["ac_hysteresis"] = _hysteresis;
    cfg["ac_door_pause_secs"] = _doorSecsToPause;
    cfg["ac_door_stop_secs"] = _doorSecsToStop;
    cfg["ac_door_control"] = _doorControlEnabled;
    cfg["ac_turn_off_time"] = _turnOffTime;
    cfg["ac_sleep_in_turn_off_time"] = _sleepInTurnOffTime;
    cfg["ac_default_target"] = _defaultTarget;
    JsonObject inputs = ac.createNestedObject("inputs");
    if (_door)
        _door->info(inputs);
}

// ---- console ------------------------------------------------------------------------

static bool parseOnOff(const String &arg, bool &out)
{
    String a = arg;
    a.trim();
    a.toUpperCase();
    if (a == "1" || a == "ON" || a == "TRUE")
        out = true;
    else if (a == "0" || a == "OFF" || a == "FALSE")
        out = false;
    else
        return false;
    return true;
}

bool AcController::handles(const String &command) const
{
    return command == "AC" || command == "DOOR" ||
           command == "SETTEMP" || command == "TARGET" || command == "POWER" ||
           command == "MANUALSYNC" || command == "SLEEP-IN" || command == "SENDIR" ||
           command == "PAUSEDOORSENSOR" || command == "SLEEP";
}

NightMareResults AcController::command(const NightMareMessage &m)
{
    NightMareResults res;
    res.result = false;

    auto ok = [&](const String &text) {
        res.result = true;
        res.response = text;
        publishState(true);
        return res;
    };
    auto fail = [&](const String &text) {
        res.result = false;
        res.response = text;
        return res;
    };

    // ---- the words the Service sends, at the root ----
    // The Service's parameters are args[0] (which the parser also exposes as subcommand).
    if (m.command == "SETTEMP")
    {
        int t = m.args[0].toInt();
        if (t < MIN_AC_TEMP || t > MAX_AC_TEMP)
            return fail("SETTEMP: " + String(MIN_AC_TEMP) + "-" + String(MAX_AC_TEMP) + " only.");
        setUnitTemperature(t);
        return ok(acIrState.toJson());
    }
    if (m.command == "TARGET")
    {
        if (!m.args[0].length())
            return fail("Usage: TARGET <temp>  (negative disables)");
        setTarget(m.args[0].toDouble());
        return ok(stateJson());
    }
    if (m.command == "POWER")
    {
        bool on;
        if (!parseOnOff(m.args[0], on))
            return fail("Usage: POWER <0|1>");
        setPower(on);
        return ok(acIrState.toJson());
    }
    if (m.command == "MANUALSYNC")
    {
        bool on;
        int t = m.args[1].toInt();
        if (!parseOnOff(m.args[0], on) || t < MIN_AC_TEMP || t > MAX_AC_TEMP)
            return fail("Usage: MANUALSYNC <0|1> <temp 18-30>");
        manualSync(on, t);
        return ok(acIrState.toJson());
    }
    if (m.command == "SLEEP-IN")
    {
        bool armed;
        if (!parseOnOff(m.args[0], armed))
            return fail("Usage: SLEEP-IN <0|1>");
        setSleepIn(armed);
        return ok(String("{\"SleepIn\":") + (_sleepIn ? "true" : "false") + "}");
    }
    if (m.command == "SENDIR")
    {
        uint32_t code = getIrCode(m.args[0]);
        if (!code)
            return fail("Unknown IR code '" + m.args[0] + "'. Known: " + getIrCodeNames());
        if (!sendIRCode(code))
            return fail("IR queue is full.");
        return ok("Queued " + getIrName(code));
    }
    if (m.command == "PAUSEDOORSENSOR")
    {
        bool paused;
        if (!parseOnOff(m.args[0], paused))
            return fail("Usage: PAUSEDOORSENSOR <0|1>");
        setDoorPaused(paused);
        return ok(String("{\"DoorState\":") + doorState() + "}");
    }
    if (m.command == "SLEEP")
    {
        setSleepMinutes(m.args[0].toInt());
        return ok(String("{\"Ssleep\":") + _swSleep + "}");
    }

    // ---- the AC group ----
    if (m.command == "AC")
    {
        if (m.subcommand == "STATE" || m.subcommand == "")
            return ok(stateJson());
        if (m.subcommand == "INFO")
        {
            DynamicJsonDocument doc(768);
            info(doc.to<JsonObject>());
            String out;
            serializeJson(doc, out);
            return ok(out);
        }
        if (m.subcommand == "MORNINGOFF")
        {
            String arg = m.args[1];
            arg.toUpperCase();
            morningOff(arg == "FORCE");
            return ok(stateJson());
        }
        if (m.subcommand == "SET")
        {
            String what = m.args[1];
            what.toUpperCase();
            NightMareMessage inner;
            inner.args[0] = m.args[2];
            inner.args[1] = m.args[3];
            if (what == "TEMP") inner.command = "SETTEMP";
            else if (what == "TARGET") inner.command = "TARGET";
            else if (what == "POWER") inner.command = "POWER";
            else if (what == "SLEEPIN") inner.command = "SLEEP-IN";
            else if (what == "SLEEP") inner.command = "SLEEP";
            else return fail("Unknown AC SET target available: [TEMP, TARGET, POWER, SLEEPIN, SLEEP].");
            return command(inner);
        }
        if (m.subcommand == "HELP")
            return ok("AC [STATE|INFO|MORNINGOFF [FORCE]|SET TEMP n|SET TARGET t|SET POWER x|SET SLEEPIN x|SET SLEEP min]. "
                      "Root: SETTEMP TARGET POWER MANUALSYNC SLEEP-IN SENDIR PAUSEDOORSENSOR SLEEP.");
        return fail("Unknown AC subcommand available: [STATE, INFO, MORNINGOFF, SET, HELP].");
    }

    // ---- the DOOR group ----
    if (m.command == "DOOR")
    {
        if (!_door)
            return fail("No door input on this device.");
        if (m.subcommand == "STATE" || m.subcommand == "")
        {
            DynamicJsonDocument doc(384);
            JsonObject root = doc.to<JsonObject>();
            _door->info(root);
            bool open;
            root["open"] = _door->asBool(open) ? (open ? "true" : "false") : "unknown";
            root["openSince"] = _doorOpenTs;
            root["pausedByDoor"] = _pausedByDoor;
            root["pausedByUser"] = _doorPausedByUser;
            String out;
            serializeJson(doc, out);
            return ok(out);
        }
        if (m.subcommand == "BIND")
        {
            if (!m.args[1].length() || !m.args[2].length())
                return fail("Usage: DOOR BIND <device> <key> [invert 0|1]   e.g. DOOR BIND Mycroft door 0");
            bool invert = false;
            parseOnOff(m.args[3], invert);
            _door->bind(m.args[1], m.args[2], invert);
            _doorOpenTs = 0;
            _pausedByDoor = false;
            _stoppedByDoor = false;
            return ok("Door bound to " + m.args[1] + "/sensors/" + m.args[2] + (invert ? " (inverted)" : ""));
        }
        if (m.subcommand == "UNBIND")
        {
            _door->unbind();
            _doorOpenTs = 0;
            _pausedByDoor = false;
            _stoppedByDoor = false;
            return ok("Door unbound.");
        }
        return fail("Unknown DOOR subcommand available: [STATE, BIND <device> <key> [invert], UNBIND].");
    }

    return fail("Not an AC command.");
}

void startAcController(float (*readTemp)(), NetworkSensor *door)
{
    gAc.begin(readTemp, door);
    Timers.create("ac_loop", AC_LOOP_SECONDS, []() { gAc.loop(); });
}
