#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

#define NM_HW_STRINGIFY_INNER(value) #value
#define NM_HW_STRINGIFY(value) NM_HW_STRINGIFY_INNER(value)
#define NM_HW_GPIO(pin) "GPIO" NM_HW_STRINGIFY(pin)

namespace NMHardware
{
// Indices into `boards`. The selected controller is both the only physical
// board described here and the board hosting this firmware.
enum AdlerBoard : uint8_t
{
    MainBoard,
};

// Indices into `devices`. Guards mirror the devices array so every endpoint
// keeps referring to the correct component on every supported board revision.
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

// Electrical nets are separate from physical connection segments in topology
// v3. Their guards mirror both the devices and connections below.
enum AdlerNet : uint8_t
{
#if BOARD_HAS_IR_RECEIVER
    IrReceiveNet,
#endif
    IrTransmitNet,
    StatusLedNet,
#if BOARD_HAS_DS18B20
    TemperatureNet,
#endif
#if defined(PIN_BUTTON)
    ButtonNet,
#endif
#if defined(PIN_RGB_DATA)
    RgbDataNet,
#if defined(PIN_RGB_POWER)
    RgbPowerEnableNet,
#endif
#endif
};

inline Profile projectProfile()
{
    static const Board boards[] = {
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
        {"ir_receiver", "TSOP demodulator", NoBoard, DeviceKind::Sensor},
#endif
        {"ir_transmitter", "IR LED 38 kHz carrier", MainBoard, DeviceKind::Led},
        {"status_led", "Onboard LED", MainBoard, DeviceKind::Led},
#if BOARD_HAS_DS18B20
        {"temperature", "DS18B20", NoBoard, DeviceKind::Sensor},
#endif
#if defined(PIN_BUTTON)
        {"button", "Onboard button", MainBoard, DeviceKind::Button},
#endif
#if defined(PIN_RGB_DATA)
        {"rgb_led", "WS2812", MainBoard, DeviceKind::Led},
#endif
    };

    static const Net nets[] = {
#if BOARD_HAS_IR_RECEIVER
        {"ir_receive", SignalType::Gpio, NoBus, Direction::Input, Pull::Up, true},
#endif
        {"ir_transmit", SignalType::Pwm, NoBus, Direction::Output},
        {"status_led", SignalType::Gpio, NoBus, Direction::Output,
         Pull::None, ONBOARD_LED_ON == LOW},
#if BOARD_HAS_DS18B20
        {"temperature", SignalType::OneWire, 0, Direction::Bidirectional,
         Pull::ExternalUp, false, Resistor("4k7")},
#endif
#if defined(PIN_BUTTON)
        {"button", SignalType::Gpio, NoBus, Direction::Input, Pull::Up, true},
#endif
#if defined(PIN_RGB_DATA)
        {"rgb_data", SignalType::Gpio, NoBus, Direction::Output},
#if defined(PIN_RGB_POWER)
        {"rgb_power_enable", SignalType::Gpio, NoBus, Direction::Output},
#endif
#endif
    };

    static const Connection connections[] = {
#if BOARD_HAS_IR_RECEIVER
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_IR_RECEIVE)},
         {EndpointKind::Device, IrReceiver, "OUT"}, IrReceiveNet},
#endif
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_IR_LED)},
         {EndpointKind::Device, IrTransmitter, "A"}, IrTransmitNet},
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_ONBOARD_LED)},
         {EndpointKind::Device, StatusLed, "A"}, StatusLedNet},
#if BOARD_HAS_DS18B20
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_ONE_WIRE)},
         {EndpointKind::Device, TemperatureProbe, "DQ"}, TemperatureNet},
#endif
#if defined(PIN_BUTTON)
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_BUTTON)},
         {EndpointKind::Device, Button, "signal"}, ButtonNet},
#endif
#if defined(PIN_RGB_DATA)
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_RGB_DATA)},
         {EndpointKind::Device, RgbLed, "DIN"}, RgbDataNet},
#if defined(PIN_RGB_POWER)
        {{EndpointKind::Board, MainBoard, NM_HW_GPIO(PIN_RGB_POWER)},
         {EndpointKind::Device, RgbLed, "power_enable"}, RgbPowerEnableNet},
#endif
#endif
    };

    return {MainBoard,
            boards, sizeof(boards) / sizeof(boards[0]),
            devices, sizeof(devices) / sizeof(devices[0]),
            nets, sizeof(nets) / sizeof(nets[0]),
            connections, sizeof(connections) / sizeof(connections[0])};
}
}

#undef NM_HW_GPIO
#undef NM_HW_STRINGIFY
#undef NM_HW_STRINGIFY_INNER
