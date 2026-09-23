#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
// Indices into `devices`. Guarded exactly like the devices array below, so a
// board only advertises the parts it actually has.
enum AdlerDevice : uint8_t
{
#if BOARD_HAS_IR_RECEIVER
    IrReceiver,
#endif
    IrTransmitter,
    StatusLed,
#if BOARD_HAS_DS18B20
    TemperatureProbe,
#endif
#if defined(PIN_BUTTON)
    Button,
#endif
#if defined(PIN_RGB_DATA)
    RgbLed,
#endif
};

inline Profile projectProfile()
{
    static const Board boards[] = {
        {"main", "MW-mycroft-X v.10"},
#if defined(BOARD_C3_V1)
        {"controller", "esp32-c3-supermini:v1"},
#elif defined(BOARD_C6_V1)
        {"controller", "m5stack-nanoc6:v1"},
#else
        {"controller", "esp32-devkit-v1:v1"},
#endif
    };
    static const Device devices[] = {
#if BOARD_HAS_IR_RECEIVER
        {"ir_receiver", "TSOP demodulator", NoBoard},
#endif
        {"ir_transmitter", "IR LED 38 kHz carrier", NoBoard},
        {"status_led", "Onboard LED", 1},
#if BOARD_HAS_DS18B20
        {"temperature", "DS18B20", NoBoard},
#endif
#if defined(PIN_BUTTON)
        {"button", "Onboard button", 1},
#endif
#if defined(PIN_RGB_DATA)
        {"rgb_led", "WS2812", 1},
#endif
    };
    static const Connection connections[] = {
#if BOARD_HAS_IR_RECEIVER
        {PIN_IR_RECEIVE, IrReceiver, "data", NoBus, SignalType::Gpio,
         Direction::Input, Pull::Up, true},
#endif
        {PIN_IR_LED, IrTransmitter, "carrier", NoBus, SignalType::Pwm,
         Direction::Output},
        {PIN_ONBOARD_LED, StatusLed, "led", NoBus, SignalType::Gpio,
         Direction::Output, Pull::None, ONBOARD_LED_ON == LOW},
#if BOARD_HAS_DS18B20
        {PIN_ONE_WIRE, TemperatureProbe, "data", 0, SignalType::OneWire,
         Direction::Bidirectional, Pull::ExternalUp, false, Resistor("4k7")},
#endif
#if defined(PIN_BUTTON)
        {PIN_BUTTON, Button, "pressed", NoBus, SignalType::Gpio,
         Direction::Input, Pull::Up, true},
#endif
#if defined(PIN_RGB_DATA)
        {PIN_RGB_DATA, RgbLed, "data", NoBus, SignalType::Gpio,
         Direction::Output},
#if defined(PIN_RGB_POWER)
        {PIN_RGB_POWER, RgbLed, "power_enable", NoBus, SignalType::Gpio,
         Direction::Output},
#endif
#endif
    };
    return {boards, sizeof(boards) / sizeof(boards[0]),
            devices, sizeof(devices) / sizeof(devices[0]),
            connections, sizeof(connections) / sizeof(connections[0])};
}
}
