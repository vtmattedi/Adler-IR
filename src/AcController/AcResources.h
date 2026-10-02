#pragma once

#include <NightMareNetwork.h>
#include <AcController/AcController.h>

// AcController's network surface. It owns the declarations, stable names, write/invoke
// handlers; NightMare binds them at startup; AcController::publishResources() keeps the Values current.
//
// Scalar states are writable where the operation is naturally idempotent; one-shot operations
// remain Actions. ac_status is the complete compatibility/state document.

/// The door is implemented by another device; the AC policy is its only consumer.
extern RemoteSensor<bool> doorSensor;

extern ManagedSensor<String> acStatus;
extern ManagedSensor<int8_t> acControllerState;
extern ManagedSensor<bool> acKnown;
extern ManagedSensor<uint8_t> acDoorState;
extern ManagedSensor<uint32_t> acSleepDeadline;
extern ManagedState<bool> acPower;
extern ManagedState<uint8_t> acUnitTemperature;
extern ManagedState<float> acTarget;
extern ManagedState<bool> acSleepIn;
extern ManagedState<bool> acDoorPaused;
extern ManagedState<int32_t> acMode;
extern ManagedState<uint8_t> acFan;
extern ManagedState<bool> acTurbo;
extern ManagedState<bool> acLed;
extern ManagedAction acManualSyncAction;
extern ManagedAction acSleepAction;
extern ManagedAction acMorningOffAction;

/// Installs the write/invoke handlers. The resources bind themselves in startNightMareESP().
void installAcResourceHandlers();
