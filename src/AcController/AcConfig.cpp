#include "AcConfig.h"

#include <IrController/IrController.h>
#include <math.h>

Config<float> acHysteresisConfig("ac_hysteresis", 0.5);
Config<uint32_t> acInputStaleSecondsConfig("ac_input_stale_secs", 600);
Config<uint32_t> acDoorPauseSecondsConfig("ac_door_pause_secs", 300);
Config<uint32_t> acDoorStopSecondsConfig("ac_door_stop_secs", 600);
Config<bool> acDoorControlConfig("ac_door_control", true);
Config<bool> doorInvertConfig("door_invert", false);
Config<TimeType> acTurnOffTimeConfig("ac_turn_off_time", TimeType(5, 30));
Config<TimeType> acSleepInTurnOffTimeConfig("ac_sleep_in_turn_off_time", TimeType(8, 30));
Config<float> acDefaultTargetConfig("ac_default_target", 24.0f);

namespace
{
bool acceptHysteresis(Config<float> &, const float &requested)
{
    return isfinite(requested) && requested > 0.0f;
}

bool acceptInputStale(Config<uint32_t> &, const uint32_t &requested)
{
    return requested > 0 && requested <= INT32_MAX / 1000UL;
}

bool acceptDoorPause(Config<uint32_t> &, const uint32_t &requested)
{
    return requested <= acDoorStopSecondsConfig.value();
}

bool acceptDoorStop(Config<uint32_t> &, const uint32_t &requested)
{
    return requested >= acDoorPauseSecondsConfig.value();
}

// The scheduler is minute-resolution, so a seconds component could never be honoured.
bool acceptWallClock(Config<TimeType> &, const TimeType &requested)
{
    return requested.second() == 0;
}

bool acceptDefaultTarget(Config<float> &, const float &requested)
{
    return isfinite(requested) && requested >= MIN_AC_TEMP && requested <= MAX_AC_TEMP;
}
} // namespace

void installAcConfigPolicy()
{
    acHysteresisConfig.onWrite = acceptHysteresis;
    acInputStaleSecondsConfig.onWrite = acceptInputStale;
    acDoorPauseSecondsConfig.onWrite = acceptDoorPause;
    acDoorStopSecondsConfig.onWrite = acceptDoorStop;
    acTurnOffTimeConfig.onWrite = acceptWallClock;
    acSleepInTurnOffTimeConfig.onWrite = acceptWallClock;
    acDefaultTargetConfig.onWrite = acceptDefaultTarget;
}
