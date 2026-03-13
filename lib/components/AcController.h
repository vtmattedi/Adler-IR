#pragma once

#include <Arduino.h>
#include <NightMareNetwork.h>
#include "IrController.h"
#include "Sensors.h"

#define DEFAULT_HYSTERESIS 0.5

class AcController
{
private:
    double targetTemp;
    double hysteresis;
    double (*getCurrentTemp)(void);
    struct CaptureAcState
    {
        double targetTemp;
        bool power;
    } pauseState;
    bool doorControlEnabled;
    unsigned long doorSecsToPause;
    unsigned long doorSecsToStop;
    unsigned long doorOpenTimestamp;
    bool sleepIn;
    String turnOffTime;
    String sleepInturnOffTime;
    void captureState();
    void restoreState();
    void temperatureControlLoop();
    bool doorsControlLoop();

public:
    // Current State Setters

    /// @brief Sets the target temperature for the AC unit. A negative value will turn off the AC.
    /// @param temp The target temperature in degrees Celsius.
    void setTargetTemperature(double temp);
    void setTargetTemperatureDelta(double delta);
    void toggleTarget();

    // Configuration Setters (Persistent)

    /// @brief Sets the number of seconds the door can be open before the AC is paused. This is a soft pause that will allow the AC to turn back on if the door is closed again.
    /// @param secs The number of seconds the door can be open before the AC is paused
    void setDoorSecondsToPause(unsigned long secs);
    /// @brief Sets the number of seconds the door can be open before the AC is stopped. This is a hard stop that will disable AC control until the door is closed and state is restored.
    /// @param secs The number of seconds the door can be open before the AC is stopped
    void setDoorSecondsToStop(unsigned long secs);
    /// @brief Enables or disables door control. When enabled, the AC will be paused or stopped based on the door open time configured by setDoorSecondsToPause and setDoorSecondsToStop. When disabled, the AC will not be affected by the door state.
    /// @param enable   True to enable door control, false to disable
    void enableDoorControl(bool enable);
    /// @brief Sets the hysteresis value for the AC control loop. This determines how much the temperature must deviate from the target before the AC is turned on or off.
    /// @param hys The hysteresis value in degrees Celsius.
    void setHysteresis(double hys);

    // Control Methods

    /// @brief The main control loop for the AC unit. This should be called periodically to check the temperature and control the AC accordingly.
    void controlLoop();
    void autoTurnOff();
    void setDoorOpen(bool open);
    void init();
};

/// @brief Starts the AC control loop. This should be called after setting up the MQTT client and sensors.
void startAcService();
extern AcController gAcController;
