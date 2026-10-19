#pragma once
#include <Arduino.h>
#include <string>

/// @brief Every IR code this device knows, across remotes. The table in IrCodes.cpp
/// pairs each with its wire name and protocol.
enum IrCodes
{
    NO_CODE = 0, ///< not in the table
    // MY AC unit -- its own 24-bit pulse-distance protocol
    AC_POWER = 0x10001,
    AC_PLUS = 0x40004,
    AC_MINUS = 0x20002,
    AC_COUNT_DOWN = 0x400040,
    AC_LED = 0x800080,
    AC_TURBO = 0x200020,
    AC_MODE = 0x100010,
    AC_VENTILATOR = 0x80008,
    AC_SLEEP1 = 0x10100,
    AC_SLEEP2 = 0x20200,
    AC_SLEEP3 = 0x40400,

    // HY350 Max projector - NEC raw values, LSB first
    HY350_POWER = 0xEB14FF00,
    HY350_UP = 0xFC03FF00,
    HY350_DOWN = 0xFD02FF00,
    HY350_LEFT = 0xF10EFF00,
    HY350_RIGHT = 0xE51AFF00,
    HY350_OK = 0xF807FF00,
    HY350_VOL_UP = 0xF40BFF00,
    HY350_VOL_DOWN = 0xA758FF00,
    HY350_MUTE = 0xFE01FF00,
    HY350_HOME = 0xB748FF00,
    HY350_BACK = 0xA35CFF00,
    HY350_LIST = 0xEC13FF00,
};

/// @brief How a code is transmitted and recognised. The AC speaks a 24-bit pulse-distance
/// protocol of its own; the HY350 projector is plain NEC, address 0x00. A received frame
/// only counts as one of ours when both the raw value and the protocol match.
enum IrProtocol
{
    IR_PROTO_NONE = 0, ///< not in the table
    IR_PROTO_AC,       ///< sendPulseDistanceWidth: 38 kHz, 9000/4550 header, 600/1700 and 600/550, 24 bits LSB first
    IR_PROTO_NEC       ///< IrSender.sendNEC(raw & 0xFF, (raw >> 16) & 0xFF, 0): the raw is ~cmd|cmd|~addr|addr
};

/// @brief Gets the wire name of a code: "POWER" for the AC, "HY350_POWER" for the projector.
/// @return The name, "UNKNOWN" if the code is not in the table.
std::string getIrName(uint32_t IrCode);
/// @brief Looks up a code by its wire name, the inverse of getIrName. Case and surrounding
/// whitespace are ignored.
/// @return The code, or IrCodes::NO_CODE when the name is not one of the known commands.
IrCodes getIrCodeByName(const std::string &name);
/// @brief Every name getIrCode() accepts, comma separated, for help text.
std::string getIrCodeNames();
/// @brief The protocol a table code belongs to, IR_PROTO_NONE for anything else.
IrProtocol getIrProtocol(uint32_t code);
