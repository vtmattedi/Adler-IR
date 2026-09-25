#pragma once

#include <NightMareNetwork.h>
#include <board.h>
#include <AcController/AcController.h>

/* Adler do not need to own the temperature sensor, but it does need a temperature reference to control the air-conditioner. The sensor may be local (DS18B20) or remote (another device, over MQTT).
 * user can configure the source device and resource name at runtime, and the
 * device will remember it across reboots.
 * For AcLoop it always just use temperatureSensor.getValue() to get the current temperature, and it will be either the local DS18B20 or the remote sensor, depending on how the user configured it.
 * This should be transparent to the rest of the application: it just reads the temperature, and the source can be changed at runtime.
 * (execpt for the tempSensor loop OFC). Therefore should always be a temperatureSensor.
 */

// Adler's resource model. These objects have static storage because the
// ResourcesManager keeps non-owning pointers to them for the life of the device.
extern ManagedSensor<float> temperatureSensor;
#if BOARD_HAS_DS18B20
#else
extern ManagedAction setTemperatureExternalSensor;
extern RemoteSensor<float> externalTemperatureSensor;
#endif

#if BOARD_HAS_IR_RECEIVER
extern ManagedSensor<String> irRecvSensor;
#endif
extern ManagedAction irSendAction;
extern RemoteSensor<bool> doorSensor;

// AC control surface. Scalar states are writable where the operation is
// naturally idempotent; one-shot operations remain actions. ac_status is the
// complete compatibility/state document.
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
#if defined(BOARD_C6_V1)
#include <ledController/ledController.h>
extern ManagedState<uint32_t> rgbLedColor;
#endif
/// Binds every resource once. The remote door source is read from the existing
/// `door_device` and `door_key` settings; an empty device leaves it unconfigured.
bool bindResources();

/// Publishes controller/IR changes into the managed AC resources. It only
/// writes fields whose effective value changed.
void syncAcResources();
