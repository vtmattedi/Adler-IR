#include "AcConfig.h"
Config<TimeType> acTime("ac:config:morning_shutdown_time", TimeType(4, 0), false);
Config<TimeType> acSleepInTime("ac:config:sleep_in_time", TimeType(2, 0), false);
