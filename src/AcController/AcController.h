#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NightMare.h>
#include <IrController/IrController.h>
#include <acGuesser.h>
#include <TempSensor/TempSensor.h>
#include "AcConfig.h"
constexpr uint32_t AC_LOOP_INTERVAL_MS = 2000;
constexpr float AC_DEFAULT_HYSTERESIS = 0.5f;
constexpr uint32_t AC_DEFAULT_INPUT_STALE_SECONDS = 600;


enum AcControllerState : int8_t
{
    AC_OFF_TARGET_DOOR_OPEN = -4,
    AC_OFF_DOOR_OPEN = -3,
    AC_OFF_TARGET = -2,
    AC_OFF = -1,
    AC_UNKNOWN = 0,
    AC_ON = 1,
    AC_ON_TARGET = 2,
};

/// Adler's air-conditioner policy. The IR controller owns the physical-unit
/// belief; this class owns thermostat, door, sleep and daily-shutdown policy.
class AcController
{
public:
    /// Installs config policy and resource handlers. Call before startNightMareESP().
    void begin();
    void loop();
    /// Publishes controller/IR changes into the managed AC resources. Writes only fields
    /// whose effective value changed.
    void publishResources();

    bool setUnitTemperature(uint8_t temperature);
    bool setTarget(float target); // Negative disables thermostat control.
    bool setPower(bool on);
    bool setMode(AcIrMode mode);
    bool setFan(uint8_t fan);
    bool setTurbo(bool enabled);
    bool setLed(bool enabled);
    bool manualSync(bool on, uint8_t temperature);
    void setSleepIn(bool armed);
    void setDoorPaused(bool paused);
    bool setSleepMinutes(uint32_t minutes); // Zero cancels.
    void morningOff(bool force);

    AcControllerState state() const;
    uint8_t doorState() const;
    float target() const { return target_; }
    bool sleepIn() const { return sleepIn_; }
    bool doorPaused() const { return doorPausedByUser_; }
    uint32_t sleepDeadline() const { return sleepDeadlineEpoch_; }
    bool currentTemperature(float &temperature) const;
    String stateJson() const;
    void info(JsonObject into) const;

    bool handles(const String &command) const;
    NightMareResults command(const NightMareMessage &message);

private:
    float hysteresis_ = AC_DEFAULT_HYSTERESIS;
    uint32_t inputStaleMs_ = AC_DEFAULT_INPUT_STALE_SECONDS * 1000UL;
    uint32_t doorSecsToPause_ = 300;
    uint32_t doorSecsToStop_ = 600;
    bool doorControlEnabled_ = true;
    bool doorInvert_ = false;
    TimeType turnOffTime_{5, 30};
    TimeType sleepInTurnOffTime_{8, 30};
    float defaultTarget_ = 24.0f;

    float target_ = -24.0f;
    bool sleepIn_ = false;
    bool doorPausedByUser_ = false;
    bool pausedByDoor_ = false;
    bool stoppedByDoor_ = false;
    bool resumePowerAfterDoor_ = false;
    bool doorOpen_ = false;
    uint32_t doorOpenMs_ = 0;
    uint32_t doorOpenEpoch_ = 0;
    bool sleepArmed_ = false;
    uint32_t sleepDeadlineMs_ = 0;
    uint32_t sleepDeadlineEpoch_ = 0;
    uint32_t lastLoopMs_ = 0;
    bool tasksEnsured_ = false;

    void syncConfig();
    void doorLoop();
    void targetLoop();
    void sleepLoop();
    void shutdownUnit();
    void ensureScheduledTasks();
    bool scheduleMorningOff(bool forced);
    static void morningOffJob();
    static void sleepInOffJob();
};

extern AcController gAc;
