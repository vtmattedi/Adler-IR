#pragma once
#include <Arduino.h>
#include <NightMare/Runtime/Scheduler.h>
#include <DoorInput.h>
#include <IrController.h>

enum class AcState : int8_t {
    OFF_TARGET_DOOR_OPEN = -4, OFF_DOOR_OPEN = -3, OFF_TARGET = -2,
    OFF = -1, UNKNOWN = 0, ON = 1, ON_TARGET = 2
};

// AC policy and hardware behavior. Network-visible state and commands are
// registered as Resources in main.cpp, outside this service.
class AcController {
public:
    void begin(float (*readTemp)(), DoorInput* door, NightMare::Scheduler& scheduler);
    void loop();
    void reloadSettings();
    void setUnitTemperature(uint8_t temp);
    void setTarget(double target);
    void setPower(bool on);
    void manualSync(bool on, uint8_t temp);
    void setSleepIn(bool armed);
    void setDoorPaused(bool paused);
    void setSleepMinutes(uint32_t minutes);
    void morningOff(bool force);
    AcState state() const;
    uint8_t doorState() const;
    double target() const { return _target; }
    bool sleepIn() const { return _sleepIn; }
    bool doorPaused() const { return _doorPausedByUser; }
    uint32_t sleepRemainingSeconds() const;
private:
    float (*_readTemp)() = nullptr;
    DoorInput* _door = nullptr;
    NightMare::Scheduler* _scheduler = nullptr;
    double _hysteresis = 0.5;
    uint32_t _doorSecsToPause = 300;
    uint32_t _doorSecsToStop = 600;
    bool _doorControlEnabled = true;
    String _turnOffTime = "05:30";
    String _sleepInTurnOffTime = "08:30";
    int16_t _utcOffsetMinutes = -180;
    double _defaultTarget = 24.0;
    double _target = -24.0;
    bool _sleepIn = false;
    bool _doorPausedByUser = false;
    bool _pausedByDoor = false;
    bool _stoppedByDoor = false;
    bool _resumePowerAfterDoor = false;
    bool _doorOpen = false;
    uint32_t _doorOpenMs = 0;
    uint32_t _sleepDeadlineMs = 0;
    void doorLoop();
    void targetLoop();
    void sleepLoop();
    void scheduleMorningJobs();
};

extern AcController gAc;
