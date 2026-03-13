#include "AcController.h"

AcController gAcController;

/// target methods

void AcController::setTargetTemperature(double temp)
{
    targetTemp = temp;
}

void AcController::setTargetTemperatureDelta(double delta)
{
    targetTemp += delta;
}

void AcController::toggleTarget()
{
    targetTemp *= -1;
}

/// Persistent configuration methods

void AcController::setHysteresis(double hys)
{
    hysteresis = hys;
    Config.set("ac_hysteresis", String(hys));
    Config.save();
}

void AcController::setDoorSecondsToPause(unsigned long secs)
{
    doorSecsToPause = secs;
    Config.set("ac_door_pause_secs", String(secs));
    Config.save();
}
void AcController::setDoorSecondsToStop(unsigned long secs)
{
    doorSecsToStop = secs;
    Config.set("ac_door_stop_secs", String(secs));
    Config.save();
}

/// control Methods

void AcController::captureState()
{
    pauseState.targetTemp = targetTemp;
    pauseState.power = currentIrState.power;
}

void AcController::restoreState()
{
    targetTemp = pauseState.targetTemp;
    currentIrState.setPower(pauseState.power);
}

void AcController::setDoorOpen(bool open)
{
    if (open)
    {
        doorOpenTimestamp = now();
    }
    else
    {
        doorOpenTimestamp = 0;
    }
}

void AcController::temperatureControlLoop()
{
    // Negative target means AC control disabled.
    if (targetTemp < 0 || getCurrentTemp == nullptr)
        return;
    const double currentTemp = getCurrentTemp();
    double delta = currentTemp - targetTemp;
    if (delta > hysteresis)
    {
        currentIrState.setPower(true); // Too hot, need to turn on cooling
    }
    else if (delta < -hysteresis)
    {
        currentIrState.setPower(false); // Too cold, turn off cooling
    }
}

bool AcController::doorsControlLoop()
{
    if (doorOpenTimestamp > 0)
    {
        if (now() - doorOpenTimestamp > doorSecsToPause) // if the door has been open for more than the configured time
        {
            currentIrState.setPower(false); // turn off the AC to save energy
            return false;                   // skip the rest of the control loop while the door is open
        }
        if (now() - doorOpenTimestamp > doorSecsToStop) // if the door has been open for more than the configured time
        {
            targetTemp = -1;                // disable AC control to prevent it from turning back on until the door is closed and state is restored
            currentIrState.setPower(false); // turn off the AC to save energy
            this->setDoorOpen(false);       // After we stop the AC due to the door being open for too long, we can consider user has left the room and we can reset the door state to closed to allow the AC to turn back on when the door is opened again.
            return false;                   // skip the rest of the control loop while the door is open
        }
    }
    return true;
}
void AcController::controlLoop()
{
    // run all the control loops here.
    // we will need to:
    // 1. handle door open/close
    // 2. handle Target temperature control
    // 3. handle sleep on Timer

    if (!this->doorsControlLoop())
        return;
    this->temperatureControlLoop();
}

void AcController::autoTurnOff()
{
    // Expensive compare but w/e
    String currTime = TIME_STR(now());

    if (this->sleepIn ? currTime == this->sleepInturnOffTime : currTime == this->turnOffTime)
    {
        currentIrState.setPower(false);
        if (targetTemp > 0)
        {
            this->toggleTarget();
        }
        this->sleepIn = false;
    }
}

void AcController::init()
{
    // Load configuration values or use defaults
    hysteresis = Config.get("ac_hysteresis", String(DEFAULT_HYSTERESIS)).toDouble();
    doorSecsToPause = Config.get("ac_door_pause_secs", "300").toInt();
    doorSecsToStop = Config.get("ac_door_stop_secs", "600").toInt();
    doorControlEnabled = Config.get("ac_door_control", "1") == "1";
    turnOffTime = Config.get("ac_turn_off_time", "05:30");
    sleepInturnOffTime = Config.get("ac_sleep_in_turn_off_time", "08:30");
    // Saves back read values to ensure they are stored in config if they were not already, and to apply any defaults.
    Config.set("ac_hysteresis", String(hysteresis));
    Config.set("ac_door_pause_secs", String(doorSecsToPause));
    Config.set("ac_door_stop_secs", String(doorSecsToStop));
    Config.setFlag("ac_door_control", doorControlEnabled );
    Config.set("ac_turn_off_time", turnOffTime);
    Config.set("ac_sleep_in_turn_off_time", sleepInturnOffTime);
    Config.save();
    sleepIn = false;
    // Set the method to get current temperature. This allows us to abstract away the actual sensor reading and makes it easier to test.
    // Later we may have different methods to get the current temperature, such as from a remote sensor or from a different type of sensor, so having this as a function pointer allows us to easily switch between them without changing the rest of the code.
    getCurrentTemp = getTemperature;
    // Initialize the target temperature to a default value. This can be changed by the user later.
    // A negative target temperature will be used to indicate that AC control is disabled, so we can use that as the default value to start with.
    targetTemp = -24.0;
}

void startAcService()
{
    // loads saved hysteresis value from config, or uses default if not set
    gAcController.init();
    // Captureless lambda can be used where a plain function pointer is required.
    Timers.create("ac_control_loop", 2, []()
                  { gAcController.controlLoop(); });
    Timers.create("ac_turn_off_timer", 60, []()
                  { gAcController.autoTurnOff(); });
}