#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <NightMareNetwork.h>
#include <Services/AcStates.h> // the AcState enum the Dashboard's Service reads
#include "IrController.h"
#include "NetworkSensor.h"

// The device-side AC controller. It owns the policy -- hold a room temperature,
// stand down while the door is open, shut down in the morning -- and drives the
// IR actuator to do it. It speaks the vocabulary the Dashboard's AcController
// Service sends (SETTEMP, TARGET, POWER, MANUALSYNC, SLEEP-IN, SENDIR,
// PAUSEDOORSENSOR) and publishes the state document that Service parses:
//   {AcState, DoorState, Temp, Settemp, CurrTemp, Hsleep, Ssleep, Door, SleepIn}
// Sign conventions are load-bearing: Temp < 0 means the unit is off, Settemp < 0
// means the target is disabled. The Service derives on/off from them.

#define AC_LOOP_SECONDS 2
#define AC_STATE_HEARTBEAT_SECONDS 60
#define DEFAULT_HYSTERESIS 0.5

class AcController
{
public:
    /// @param readTemp Room temperature source; NAN when unknown.
    /// @param door Door input, already bound (or not) from config. May be nullptr.
    void begin(float (*readTemp)(), NetworkSensor *door);
    /// @brief The control loop: door, then sleep, then target, then the scheduler tasks.
    void loop();

    // ---- the Service vocabulary ----
    void setUnitTemperature(uint8_t temp);   ///< SETTEMP <n>
    void setTarget(double target);           ///< TARGET <t>, negative disables
    void setPower(bool on);                  ///< POWER <0|1>
    void manualSync(bool on, uint8_t temp);  ///< MANUALSYNC <on> <temp>: corrects the belief, sends nothing
    void setSleepIn(bool armed);             ///< SLEEP-IN <0|1>: tonight's early shutdown is skipped
    void setDoorPaused(bool paused);         ///< PAUSEDOORSENSOR <0|1>: door logic off/on
    void setSleepMinutes(uint32_t minutes);  ///< SLEEP <min>: turn off after that long; 0 cancels
    /// @brief The morning shutdown. Without force, an armed sleep-in makes it a no-op; with
    /// force it always runs and consumes the sleep-in.
    void morningOff(bool force);

    // ---- reports ----
    AcState state() const;
    uint8_t doorState() const;
    String stateJson() const;
    void info(JsonObject into) const;
    /// @brief Publishes /state when it changed, or unconditionally with force.
    void publishState(bool force = false);

    // ---- console ----
    /// @brief Whether `command` (uppercased) is one of ours: the Service words or AC / DOOR.
    bool handles(const String &command) const;
    NightMareResults command(const NightMareMessage &m);

    double target() const { return _target; }

private:
    float (*_readTemp)() = nullptr;
    NetworkSensor *_door = nullptr;

    // persistent config, namespaced ac_*
    double _hysteresis = DEFAULT_HYSTERESIS;
    uint32_t _doorSecsToPause = 300;
    uint32_t _doorSecsToStop = 600;
    bool _doorControlEnabled = true;
    String _turnOffTime = "05:30";
    String _sleepInTurnOffTime = "08:30";
    double _defaultTarget = 24.0;
    bool _configDirty = false;

    // runtime
    double _target = -24.0;      ///< negative = control disabled
    bool _sleepIn = false;
    bool _doorPausedByUser = false;
    bool _pausedByDoor = false;  ///< we turned it off because the door was open
    bool _stoppedByDoor = false; ///< and gave up on the target
    bool _resumePowerAfterDoor = false;
    uint32_t _doorOpenTs = 0;    ///< epoch seconds; 0 closed
    uint32_t _swSleep = 0;       ///< epoch seconds to turn off; 0 none
    bool _tasksEnsured = false;
    uint32_t _lastPublishMs = 0;
    String _lastStateJson;

    void loadConfig();
    void doorLoop();
    void targetLoop();
    void sleepLoop();
    void ensureScheduledTasks();
    static void onConfigChanged(const String &key, const String &value);
};

extern AcController gAc;

/// @brief Loads config, wires the inputs, registers the loop timer.
void startAcController(float (*readTemp)(), NetworkSensor *door);
