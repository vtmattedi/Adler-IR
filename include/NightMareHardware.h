#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static constexpr Connection connections[] = {
#if BOARD_HAS_IR_RECEIVER
        {"IR receiver", PIN_IR_RECEIVE, Direction::Input, Pull::Up, true, "TSOP demodulator"},
#endif
        {"IR transmitter", PIN_IR_LED, Direction::Output, Pull::None, false, "IR LED 38 kHz carrier"},
        {"Status LED", PIN_ONBOARD_LED, Direction::Output, Pull::None, ONBOARD_LED_ON == LOW, "Onboard LED"},
#if BOARD_HAS_DS18B20
        {"DS18B20", PIN_ONE_WIRE, Direction::Bus, Pull::External, false, "4.7k pull-up to 3V3"},
#endif
#if defined(PIN_BUTTON)
        {"Button", PIN_BUTTON, Direction::Input, Pull::Up, true, "Onboard button"},
#endif
#if defined(PIN_RGB_DATA)
        {"RGB LED", PIN_RGB_DATA, Direction::Output, Pull::None, false, "Onboard WS2812 data"},
#endif
#if defined(PIN_RGB_POWER)
        {"RGB power", PIN_RGB_POWER, Direction::Output, Pull::None, false, "Drive HIGH to enable WS2812"},
#endif
    };
    return {BOARD_NAME, connections, sizeof(connections) / sizeof(connections[0])};
}
}
