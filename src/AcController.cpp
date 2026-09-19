#include <AcController.h>
#include <AdlerSettings.h>
#include <math.h>

AcController gAc;

static bool parseLocalTime(const String& text, int16_t offsetMinutes,
                           uint8_t& utcHour, uint8_t& utcMinute) {
    if (text.length() != 5 || text[2] != ':' ||
        !isDigit(text[0]) || !isDigit(text[1]) ||
        !isDigit(text[3]) || !isDigit(text[4])) return false;
    int hour = (text[0] - '0') * 10 + text[1] - '0';
    int minute = (text[3] - '0') * 10 + text[4] - '0';
    if (hour > 23 || minute > 59) return false;
    int utc = (hour * 60 + minute - offsetMinutes) % 1440;
    if (utc < 0) utc += 1440;
    utcHour = utc / 60;
    utcMinute = utc % 60;
    return true;
}

void AcController::begin(float (*readTemp)(), DoorInput* door,
                         NightMare::Scheduler& scheduler) {
    _readTemp = readTemp;
    _door = door;
    _scheduler = &scheduler;
    reloadSettings();
    _target = -fabs(_defaultTarget);
}

void AcController::reloadSettings() {
    _hysteresis = settings.get("ac_hysteresis", "0.5").toDouble();
    _doorSecsToPause = settings.get("ac_door_pause_secs", "300").toInt();
    _doorSecsToStop = settings.get("ac_door_stop_secs", "600").toInt();
    _doorControlEnabled = settings.get("ac_door_control", "1") == "1";
    _turnOffTime = settings.get("ac_turn_off_time", "05:30");
    _sleepInTurnOffTime = settings.get("ac_sleep_in_turn_off_time", "08:30");
    _utcOffsetMinutes = settings.get("ac_utc_offset_minutes", "-180").toInt();
    _defaultTarget = settings.get("ac_default_target", "24").toDouble();
    scheduleMorningJobs();
}

void AcController::scheduleMorningJobs() {
    if (!_scheduler) return;
    _scheduler->cancel("ac_morning_off");
    _scheduler->cancel("ac_sleep_in_off");
    uint8_t hour, minute;
    if (parseLocalTime(_turnOffTime, _utcOffsetMinutes, hour, minute))
        _scheduler->dailyAt("ac_morning_off", hour, minute,
                            [](void*) { gAc.morningOff(false); });
    if (parseLocalTime(_sleepInTurnOffTime, _utcOffsetMinutes, hour, minute))
        _scheduler->dailyAt("ac_sleep_in_off", hour, minute,
                            [](void*) { gAc.morningOff(true); });
}

void AcController::loop() {
    doorLoop();
    sleepLoop();
    targetLoop();
}

void AcController::doorLoop() {
    if (!_doorControlEnabled || _doorPausedByUser || !_door) return;
    bool open;
    if (!_door->read(open)) return; // Stale or unknown is never interpreted as closed.
    uint32_t nowMs = millis();
    if (open) {
        if (!_doorOpen) {
            _doorOpen = true;
            _doorOpenMs = nowMs;
        }
        uint32_t openFor = static_cast<uint32_t>(nowMs - _doorOpenMs) / 1000UL;
        if (!_pausedByDoor && openFor >= _doorSecsToPause) {
            _resumePowerAfterDoor = acIrState.state.power;
            if (acIrState.state.power) acIrState.setPower(false);
            _pausedByDoor = true;
            Serial.printf("[ac] door open %lus: paused\n", static_cast<unsigned long>(openFor));
        }
        if (_pausedByDoor && !_stoppedByDoor && openFor >= _doorSecsToStop) {
            if (_target > 0) _target = -_target;
            _resumePowerAfterDoor = false;
            _stoppedByDoor = true;
            Serial.printf("[ac] door open %lus: stopped\n", static_cast<unsigned long>(openFor));
        }
    } else if (_doorOpen) {
        _doorOpen = false;
        if (_pausedByDoor && !_stoppedByDoor && _resumePowerAfterDoor)
            acIrState.setPower(true);
        _pausedByDoor = false;
        _stoppedByDoor = false;
        _resumePowerAfterDoor = false;
    }
}

void AcController::sleepLoop() {
    if (_sleepDeadlineMs && static_cast<int32_t>(millis() - _sleepDeadlineMs) >= 0) {
        _sleepDeadlineMs = 0;
        if (_target > 0) _target = -_target;
        acIrState.setPower(false);
    }
}

void AcController::targetLoop() {
    if (_target < 0 || _pausedByDoor || !_readTemp) return;
    float current = _readTemp();
    if (isnan(current)) return;
    double delta = current - _target;
    if (delta > _hysteresis) acIrState.setPower(true);
    else if (delta < -_hysteresis) acIrState.setPower(false);
}

void AcController::setUnitTemperature(uint8_t temp) { acIrState.setTemp(temp); }
void AcController::setTarget(double target) {
    _target = target;
    _stoppedByDoor = false;
    if (_target > 0) _defaultTarget = _target;
}
void AcController::setPower(bool on) {
    acIrState.setPower(on);
    _pausedByDoor = false;
    _stoppedByDoor = false;
}
void AcController::manualSync(bool on, uint8_t temp) { acIrState.manualSync(on, temp); }
void AcController::setSleepIn(bool armed) { _sleepIn = armed; }
void AcController::setDoorPaused(bool paused) {
    _doorPausedByUser = paused;
    if (paused && _pausedByDoor) {
        if (_resumePowerAfterDoor) acIrState.setPower(true);
        _pausedByDoor = false;
        _stoppedByDoor = false;
    }
}
void AcController::setSleepMinutes(uint32_t minutes) {
    _sleepDeadlineMs = minutes ? millis() + minutes * 60000UL : 0;
}
uint32_t AcController::sleepRemainingSeconds() const {
    if (!_sleepDeadlineMs) return 0;
    int32_t remaining = static_cast<int32_t>(_sleepDeadlineMs - millis());
    return remaining > 0 ? static_cast<uint32_t>(remaining) / 1000UL : 0;
}
void AcController::morningOff(bool force) {
    if (!force && _sleepIn) return;
    _sleepIn = false;
    if (_target > 0) _target = -_target;
    if (acIrState.state.power) acIrState.setPower(false);
}

AcState AcController::state() const {
    if (!acIrState.known) return AcState::UNKNOWN;
    bool targeting = _target > 0;
    if (acIrState.state.power) return targeting ? AcState::ON_TARGET : AcState::ON;
    if (_pausedByDoor) return targeting ? AcState::OFF_TARGET_DOOR_OPEN : AcState::OFF_DOOR_OPEN;
    return targeting ? AcState::OFF_TARGET : AcState::OFF;
}

uint8_t AcController::doorState() const {
    uint8_t bits = 0;
    if (_doorOpen) bits |= 1 << 0;
    if (_doorPausedByUser) bits |= 1 << 1;
    if (_pausedByDoor) bits |= 1 << 2;
    return bits;
}
