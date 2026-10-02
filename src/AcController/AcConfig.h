#pragma once

#include <NightMare/Config.h>

// The AC policy's local parameters. AcController owns them: each Config registers itself
// with the ConfigManager when constructed, and validates its own ingress through a typed
// onWrite handler installed by installAcConfigPolicy().
extern Config<float> acHysteresisConfig;
extern Config<uint32_t> acInputStaleSecondsConfig;
extern Config<uint32_t> acDoorPauseSecondsConfig;
extern Config<uint32_t> acDoorStopSecondsConfig;
extern Config<bool> acDoorControlConfig;
extern Config<bool> doorInvertConfig;
extern Config<TimeType> acTurnOffTimeConfig;
extern Config<TimeType> acSleepInTurnOffTimeConfig;
extern Config<float> acDefaultTargetConfig;

/// Installs each Config's write policy. Called by AcController::begin().
void installAcConfigPolicy();
