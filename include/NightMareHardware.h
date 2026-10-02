#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{

inline Profile projectProfile()
{

// =============================================================================
// Adler — M5Stack NanoC6
//
// No external hardware is currently connected.
// For now this profile identifies/documents the physical controller board.
// =============================================================================

#if defined(BOARD_C6_V1)

    static const HardwareDefinition definitions[] = {
        {
            "m5stack-nanoc6-rev1",
            AssemblyKind::MarketBoard,
            "M5Stack NanoC6 rev1",
            "M5Stack NanoC6",
            "M5Stack",
        },
    };

    static const Assembly roots[] = {
        {
            "controller",
            "m5stack-nanoc6-rev1",
            "Adler",
        },
    };

    return {
        "controller",
        definitions,
        sizeof(definitions) / sizeof(definitions[0]),
        roots,
        sizeof(roots) / sizeof(roots[0]),
        nullptr,
        0,
    };


// =============================================================================
// Adler — ESP32-C3 SuperMini + MycroftY
//
// Physical layout:
//
//   Hisense AC
//       5V/GND
//          |
//          v
//      MycroftY
//          |
//          | 2-pin 5V/GND
//          v
//      ESP32-C3 VIN
//
//
//   ESP32-C3                 MycroftY
//
//   GPIO7  ----------------> IO IN.4 <----> IO OUT.4 ---> IR receiver
//   GPIO8  ----------------> IO IN.3 <----> IO OUT.3 ---> IR emitter
//   GPIO9  ----------------> IO IN.2 <----> IO OUT.2 ---> open
//   GPIO10 ----------------> IO IN.1 <----> IO OUT.1 ---> DS18B20 DQ
//                                                        |
//                                                        4k7
//                                                        |
//                                                       3V3
//
//   ESP32-C3 3V3/GND ------> MycroftY logic power
//
// 5V is only used to feed the ESP32-C3 VIN.
// MycroftY logic and DS18B20 use the C3's regulated 3V3.
// =============================================================================

#elif defined(BOARD_C3_V1)


// =============================================================================
// ESP32-C3 SuperMini
// =============================================================================

    static const Terminal c3McuTerminals[] = {
        {"GPIO7"},
        {"GPIO8"},
        {"GPIO9"},
        {"GPIO10"},

        {"3V3", CanonicalNet::V3v3},
        {"GND", CanonicalNet::Gnd},
    };

    static const Device c3Devices[] = {
        {
            "mcu",
            c3McuTerminals,
            sizeof(c3McuTerminals) / sizeof(c3McuTerminals[0]),
            "ESP32-C3",
            "mcu",
            "ESP32-C3",
            "Espressif",
        },
    };


    // Four IO lines connected to MycroftY IO IN.
    static const ConnectorContact c3IoContacts[] = {
        {"GPIO7"},
        {"GPIO8"},
        {"GPIO9"},
        {"GPIO10"},
    };


    // 5V/GND arriving from MycroftY.
    //
    // VIN is deliberately not marked CanonicalNet::V5v in the reusable
    // board definition. The deployed connection to MycroftY gives this
    // particular net its +5V identity.
    static const ConnectorContact c3VinContacts[] = {
        {"VIN"},
        {"GND", CanonicalNet::Gnd},
    };


    // Regulated logic power supplied by the ESP32-C3 board to MycroftY.
    static const ConnectorContact c3LogicPowerContacts[] = {
        {"3V3", CanonicalNet::V3v3},
        {"GND", CanonicalNet::Gnd},
    };


    static const Connector c3Connectors[] = {
        {
            "io",
            c3IoContacts,
            sizeof(c3IoContacts) / sizeof(c3IoContacts[0]),
            "GPIO7-GPIO10",
            ConnectorKind::Header,
        },

        {
            "vin",
            c3VinContacts,
            sizeof(c3VinContacts) / sizeof(c3VinContacts[0]),
            "VIN",
            ConnectorKind::Header,
        },

        {
            "logic_power",
            c3LogicPowerContacts,
            sizeof(c3LogicPowerContacts) /
                sizeof(c3LogicPowerContacts[0]),
            "3V3 logic power",
            ConnectorKind::Header,
        },
    };


    static const Connection c3Connections[] = {

        // IO

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "GPIO7"},
            {"", EndpointKind::ConnectorContact, "io", "GPIO7"},
        },

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "GPIO8"},
            {"", EndpointKind::ConnectorContact, "io", "GPIO8"},
        },

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "GPIO9"},
            {"", EndpointKind::ConnectorContact, "io", "GPIO9"},
        },

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "GPIO10"},
            {"", EndpointKind::ConnectorContact, "io", "GPIO10"},
        },


        // C3 regulated logic supply.

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "3V3"},
            {"", EndpointKind::ConnectorContact, "logic_power", "3V3"},
        },

        {
            {"", EndpointKind::DeviceTerminal, "mcu", "GND"},
            {"", EndpointKind::ConnectorContact, "logic_power", "GND"},
        },

        // VIN GND and logic GND are the same board ground.
        {
            {"", EndpointKind::ConnectorContact, "vin", "GND"},
            {"", EndpointKind::DeviceTerminal, "mcu", "GND"},
        },
    };


// =============================================================================
// IR receiver module
//
// Power wiring is intentionally not invented here. The known/relevant
// connection is its signal output to MycroftY IO4.
// =============================================================================

    static const Terminal irReceiverTerminals[] = {
        {"OUT"},
    };

    static const Device irReceiverDevices[] = {
        {
            "receiver",
            irReceiverTerminals,
            sizeof(irReceiverTerminals) /
                sizeof(irReceiverTerminals[0]),
            "IR receiver",
            "ir_receiver",
            "IR demodulator module",
        },
    };

    static const ConnectorContact irReceiverContacts[] = {
        {"OUT"},
    };

    static const Connector irReceiverConnectors[] = {
        {
            "signal",
            irReceiverContacts,
            sizeof(irReceiverContacts) /
                sizeof(irReceiverContacts[0]),
            "Signal",
            ConnectorKind::DirectWire,
        },
    };

    static const Connection irReceiverConnections[] = {
        {
            {"", EndpointKind::ConnectorContact, "signal", "OUT"},
            {"", EndpointKind::DeviceTerminal, "receiver", "OUT"},
        },
    };


// =============================================================================
// IR emitter module
//
// Represents the traditional transistor + IR LED blaster circuit as one
// functional module.
// =============================================================================

    static const Terminal irEmitterTerminals[] = {
        {"IN"},
    };

    static const Device irEmitterDevices[] = {
        {
            "emitter",
            irEmitterTerminals,
            sizeof(irEmitterTerminals) /
                sizeof(irEmitterTerminals[0]),
            "IR emitter",
            "ir_emitter",
            "Transistor IR emitter",
        },
    };

    static const ConnectorContact irEmitterContacts[] = {
        {"IN"},
    };

    static const Connector irEmitterConnectors[] = {
        {
            "signal",
            irEmitterContacts,
            sizeof(irEmitterContacts) /
                sizeof(irEmitterContacts[0]),
            "Control",
            ConnectorKind::DirectWire,
        },
    };

    static const Connection irEmitterConnections[] = {
        {
            {"", EndpointKind::ConnectorContact, "signal", "IN"},
            {"", EndpointKind::DeviceTerminal, "emitter", "IN"},
        },
    };


// =============================================================================
// MycroftY
// =============================================================================

    static const Assembly mycroftChildren[] = {
        {
            "ir_receiver",
            "adler-ir-receiver",
            "IR receiver module",
        },

        {
            "ir_emitter",
            "adler-ir-emitter",
            "IR emitter module",
        },
    };


// -----------------------------------------------------------------------------
// Onboard DS18B20
// -----------------------------------------------------------------------------

    static const Terminal ds18b20Terminals[] = {
        {"VDD", CanonicalNet::V3v3},
        {"DQ"},
        {"GND", CanonicalNet::Gnd},
    };

    static const Device ds18b20Devices[] = {
        {
            "temperature",
            ds18b20Terminals,
            sizeof(ds18b20Terminals) /
                sizeof(ds18b20Terminals[0]),
            "Onboard temperature sensor",
            "sensor",
            "DS18B20",
        },
    };


// -----------------------------------------------------------------------------
// DS18B20 4k7 pull-up
//
// DQ ---- 4k7 ---- 3V3
// -----------------------------------------------------------------------------

    static const Terminal pullupTerminals[] = {
        {"DQ"},
        {"3V3", CanonicalNet::V3v3},
    };

    static const Device pullupDevices[] = {
        {
            "temperature_pullup",
            pullupTerminals,
            sizeof(pullupTerminals) /
                sizeof(pullupTerminals[0]),
            "DS18B20 pull-up",
            "resistor",
            "4.7k",
        },
    };


// -----------------------------------------------------------------------------
// MycroftY connectors
// -----------------------------------------------------------------------------

    static const ConnectorContact ioInContacts[] = {
        {"IO1"},
        {"IO2"},
        {"IO3"},
        {"IO4"},
    };

    static const ConnectorContact ioOutContacts[] = {
        {"IO1"},
        {"IO2"},
        {"IO3"},
        {"IO4"},
    };


    // 5V/GND from the Hisense AC.
    //
    // This 5V is only passed through to the ESP32-C3 VIN.
    static const ConnectorContact hisensePowerContacts[] = {
        {"5V", CanonicalNet::V5v},
        {"GND", CanonicalNet::Gnd},
    };


    // 2-pin output feeding the ESP32-C3 VIN.
    static const ConnectorContact controllerVinContacts[] = {
        {"5V", CanonicalNet::V5v},
        {"GND", CanonicalNet::Gnd},
    };


    // C3-regulated 3V3 used by MycroftY logic.
    static const ConnectorContact logicPowerContacts[] = {
        {"3V3", CanonicalNet::V3v3},
        {"GND", CanonicalNet::Gnd},
    };


    static const Connector mycroftConnectors[] = {

        {
            "io_in",
            ioInContacts,
            sizeof(ioInContacts) / sizeof(ioInContacts[0]),
            "IO IN",
            ConnectorKind::Header,
        },

        {
            "io_out",
            ioOutContacts,
            sizeof(ioOutContacts) / sizeof(ioOutContacts[0]),
            "IO OUT",
            ConnectorKind::Header,
        },

        {
            "hisense_power_in",
            hisensePowerContacts,
            sizeof(hisensePowerContacts) /
                sizeof(hisensePowerContacts[0]),
            "Hisense AC 5V input",
            ConnectorKind::DirectWire,
        },

        {
            "controller_vin",
            controllerVinContacts,
            sizeof(controllerVinContacts) /
                sizeof(controllerVinContacts[0]),
            "ESP32-C3 VIN",
            ConnectorKind::Header,
        },

        {
            "logic_power",
            logicPowerContacts,
            sizeof(logicPowerContacts) /
                sizeof(logicPowerContacts[0]),
            "ESP32-C3 3V3",
            ConnectorKind::Header,
        },
    };


    static const Connection mycroftConnections[] = {

        // ---------------------------------------------------------------------
        // Reciprocal IO headers
        //
        // IO IN and IO OUT are physically distinct headers but each pair is
        // point-to-point connected on MycroftY.
        // ---------------------------------------------------------------------

        {
            {"", EndpointKind::ConnectorContact, "io_in", "IO1"},
            {"", EndpointKind::ConnectorContact, "io_out", "IO1"},
        },

        {
            {"", EndpointKind::ConnectorContact, "io_in", "IO2"},
            {"", EndpointKind::ConnectorContact, "io_out", "IO2"},
        },

        {
            {"", EndpointKind::ConnectorContact, "io_in", "IO3"},
            {"", EndpointKind::ConnectorContact, "io_out", "IO3"},
        },

        {
            {"", EndpointKind::ConnectorContact, "io_in", "IO4"},
            {"", EndpointKind::ConnectorContact, "io_out", "IO4"},
        },


        // ---------------------------------------------------------------------
        // IO4 / ESP32 GPIO7 -> IR receiver
        // ---------------------------------------------------------------------

        {
            {"", EndpointKind::ConnectorContact, "io_out", "IO4"},

            {
                "ir_receiver",
                EndpointKind::ConnectorContact,
                "signal",
                "OUT",
            },
        },


        // ---------------------------------------------------------------------
        // IO3 / ESP32 GPIO8 -> IR emitter
        // ---------------------------------------------------------------------

        {
            {"", EndpointKind::ConnectorContact, "io_out", "IO3"},

            {
                "ir_emitter",
                EndpointKind::ConnectorContact,
                "signal",
                "IN",
            },
        },


        // ---------------------------------------------------------------------
        // IO2 / ESP32 GPIO9
        //
        // Intentionally terminates at IO OUT.2.
        //
        // An LED used to be connected here but is no longer installed.
        // ---------------------------------------------------------------------


        // ---------------------------------------------------------------------
        // IO1 / ESP32 GPIO10 -> DS18B20 DQ
        // ---------------------------------------------------------------------

        {
            {"", EndpointKind::ConnectorContact, "io_out", "IO1"},
            {"", EndpointKind::DeviceTerminal, "temperature", "DQ"},
        },


        // 4k7 pull-up: DQ -> 3V3.

        {
            {"", EndpointKind::DeviceTerminal, "temperature", "DQ"},
            {
                "",
                EndpointKind::DeviceTerminal,
                "temperature_pullup",
                "DQ",
            },
        },

        {
            {
                "",
                EndpointKind::DeviceTerminal,
                "temperature_pullup",
                "3V3",
            },
            {"", EndpointKind::ConnectorContact, "logic_power", "3V3"},
        },


        // DS18B20 supply.

        {
            {"", EndpointKind::DeviceTerminal, "temperature", "VDD"},
            {"", EndpointKind::ConnectorContact, "logic_power", "3V3"},
        },

        {
            {"", EndpointKind::DeviceTerminal, "temperature", "GND"},
            {"", EndpointKind::ConnectorContact, "logic_power", "GND"},
        },


        // ---------------------------------------------------------------------
        // Hisense 5V/GND pass-through to ESP32-C3 VIN.
        //
        // Nothing else on MycroftY uses this +5V rail.
        // ---------------------------------------------------------------------

        {
            {
                "",
                EndpointKind::ConnectorContact,
                "hisense_power_in",
                "5V",
            },
            {
                "",
                EndpointKind::ConnectorContact,
                "controller_vin",
                "5V",
            },
        },

        {
            {
                "",
                EndpointKind::ConnectorContact,
                "hisense_power_in",
                "GND",
            },
            {
                "",
                EndpointKind::ConnectorContact,
                "controller_vin",
                "GND",
            },
        },
    };


// =============================================================================
// Hisense AC
//
// Only the portion relevant to Adler is modeled: its 5V/GND output.
// =============================================================================

    static const ConnectorContact hisenseOutputContacts[] = {
        {"5V", CanonicalNet::V5v},
        {"GND", CanonicalNet::Gnd},
    };

    static const Connector hisenseConnectors[] = {
        {
            "adler_power",
            hisenseOutputContacts,
            sizeof(hisenseOutputContacts) /
                sizeof(hisenseOutputContacts[0]),
            "Adler power output",
            ConnectorKind::DirectWire,
        },
    };


// =============================================================================
// Definitions
// =============================================================================

    static const HardwareDefinition definitions[] = {

        {
            "esp32-c3-supermini-adler",
            AssemblyKind::MarketBoard,
            "ESP32-C3 SuperMini",
            "ESP32-C3 SuperMini",
            nullptr,
            {
                nullptr,
                0,

                c3Devices,
                sizeof(c3Devices) / sizeof(c3Devices[0]),

                c3Connectors,
                sizeof(c3Connectors) / sizeof(c3Connectors[0]),

                c3Connections,
                sizeof(c3Connections) / sizeof(c3Connections[0]),
            },
        },


        {
            "adler-ir-receiver",
            AssemblyKind::Module,
            "IR receiver module",
            "IR demodulator",
            nullptr,
            {
                nullptr,
                0,

                irReceiverDevices,
                sizeof(irReceiverDevices) /
                    sizeof(irReceiverDevices[0]),

                irReceiverConnectors,
                sizeof(irReceiverConnectors) /
                    sizeof(irReceiverConnectors[0]),

                irReceiverConnections,
                sizeof(irReceiverConnections) /
                    sizeof(irReceiverConnections[0]),
            },
        },


        {
            "adler-ir-emitter",
            AssemblyKind::Module,
            "IR emitter module",
            "Transistor IR emitter",
            nullptr,
            {
                nullptr,
                0,

                irEmitterDevices,
                sizeof(irEmitterDevices) /
                    sizeof(irEmitterDevices[0]),

                irEmitterConnectors,
                sizeof(irEmitterConnectors) /
                    sizeof(irEmitterConnectors[0]),

                irEmitterConnections,
                sizeof(irEmitterConnections) /
                    sizeof(irEmitterConnections[0]),
            },
        },


        {
            "mycroft-y-adler",
            AssemblyKind::CustomBoard,
            "MycroftY",
            "MycroftY",
            "MattediWorks",
            {
                mycroftChildren,
                sizeof(mycroftChildren) /
                    sizeof(mycroftChildren[0]),

                // Devices are split across two arrays above, so combine them
                // below through one static array.
                nullptr,
                0,

                mycroftConnectors,
                sizeof(mycroftConnectors) /
                    sizeof(mycroftConnectors[0]),

                mycroftConnections,
                sizeof(mycroftConnections) /
                    sizeof(mycroftConnections[0]),
            },
        },


        {
            "hisense-ac-adler-source",
            AssemblyKind::External,
            "Hisense AC",
            "Hisense AC",
            "Hisense",
            {
                nullptr,
                0,

                nullptr,
                0,

                hisenseConnectors,
                sizeof(hisenseConnectors) /
                    sizeof(hisenseConnectors[0]),

                nullptr,
                0,
            },
        },
    };


// =============================================================================
// NOTE:
// AssemblyMembers accepts one contiguous Device array, so build the complete
// MycroftY definition separately with the combined device array.
// =============================================================================

    static const Device mycroftDevices[] = {

        {
            "temperature",
            ds18b20Terminals,
            sizeof(ds18b20Terminals) /
                sizeof(ds18b20Terminals[0]),
            "Onboard temperature sensor",
            "sensor",
            "DS18B20",
        },

        {
            "temperature_pullup",
            pullupTerminals,
            sizeof(pullupTerminals) /
                sizeof(pullupTerminals[0]),
            "DS18B20 pull-up",
            "resistor",
            "4.7k",
        },
    };


    static const HardwareDefinition finalDefinitions[] = {

        definitions[0],
        definitions[1],
        definitions[2],

        {
            "mycroft-y-adler",
            AssemblyKind::CustomBoard,
            "MycroftY",
            "MycroftY",
            "MattediWorks",
            {
                mycroftChildren,
                sizeof(mycroftChildren) /
                    sizeof(mycroftChildren[0]),

                mycroftDevices,
                sizeof(mycroftDevices) /
                    sizeof(mycroftDevices[0]),

                mycroftConnectors,
                sizeof(mycroftConnectors) /
                    sizeof(mycroftConnectors[0]),

                mycroftConnections,
                sizeof(mycroftConnections) /
                    sizeof(mycroftConnections[0]),
            },
        },

        definitions[4],
    };


// =============================================================================
// Deployed physical assemblies
// =============================================================================

    static const Assembly roots[] = {

        {
            "controller",
            "esp32-c3-supermini-adler",
            "Adler ESP32-C3",
        },

        {
            "mycroft",
            "mycroft-y-adler",
            "MycroftY",
        },

        {
            "hisense_ac",
            "hisense-ac-adler-source",
            "Hisense AC",
        },
    };


// =============================================================================
// Deployment wiring
// =============================================================================

    static const Connection connections[] = {

        // ---------------------------------------------------------------------
        // ESP32-C3 -> MycroftY IO IN
        //
        // Physical mapping:
        //
        // GPIO7  -> IO4
        // GPIO8  -> IO3
        // GPIO9  -> IO2
        // GPIO10 -> IO1
        // ---------------------------------------------------------------------

        {
            {"controller", EndpointKind::ConnectorContact, "io", "GPIO7"},
            {"mycroft", EndpointKind::ConnectorContact, "io_in", "IO4"},
        },

        {
            {"controller", EndpointKind::ConnectorContact, "io", "GPIO8"},
            {"mycroft", EndpointKind::ConnectorContact, "io_in", "IO3"},
        },

        {
            {"controller", EndpointKind::ConnectorContact, "io", "GPIO9"},
            {"mycroft", EndpointKind::ConnectorContact, "io_in", "IO2"},
        },

        {
            {"controller", EndpointKind::ConnectorContact, "io", "GPIO10"},
            {"mycroft", EndpointKind::ConnectorContact, "io_in", "IO1"},
        },


        // ---------------------------------------------------------------------
        // Hisense AC -> MycroftY 5V/GND
        // ---------------------------------------------------------------------

        {
            {
                "hisense_ac",
                EndpointKind::ConnectorContact,
                "adler_power",
                "5V",
            },

            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "hisense_power_in",
                "5V",
            },
        },

        {
            {
                "hisense_ac",
                EndpointKind::ConnectorContact,
                "adler_power",
                "GND",
            },

            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "hisense_power_in",
                "GND",
            },
        },


        // ---------------------------------------------------------------------
        // MycroftY -> ESP32-C3 VIN
        //
        // 5V goes only to the C3 board.
        // ---------------------------------------------------------------------

        {
            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "controller_vin",
                "5V",
            },

            {
                "controller",
                EndpointKind::ConnectorContact,
                "vin",
                "VIN",
            },
        },

        {
            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "controller_vin",
                "GND",
            },

            {
                "controller",
                EndpointKind::ConnectorContact,
                "vin",
                "GND",
            },
        },


        // ---------------------------------------------------------------------
        // ESP32-C3 regulated 3V3 -> MycroftY logic
        //
        // This powers the onboard DS18B20 and its 4k7 pull-up.
        // ---------------------------------------------------------------------

        {
            {
                "controller",
                EndpointKind::ConnectorContact,
                "logic_power",
                "3V3",
            },

            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "logic_power",
                "3V3",
            },
        },

        {
            {
                "controller",
                EndpointKind::ConnectorContact,
                "logic_power",
                "GND",
            },

            {
                "mycroft",
                EndpointKind::ConnectorContact,
                "logic_power",
                "GND",
            },
        },
    };


    return {
        "controller",

        finalDefinitions,
        sizeof(finalDefinitions) / sizeof(finalDefinitions[0]),

        roots,
        sizeof(roots) / sizeof(roots[0]),

        connections,
        sizeof(connections) / sizeof(connections[0]),
    };


#else

#error "Adler NightMareHardware.h: unsupported board revision"

#endif
}

}