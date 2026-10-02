#include "IrCodes.h"

// ---- the code table ------------------------------------------------------------
// One table drives name -> code, code -> name and code -> protocol, so the three
// can never disagree. Wire names for the AC stay unprefixed: they are what the
// Dashboard's AcController Service sends as `SENDIR <name>`.

struct IrCodeEntry
{
    const char *name;
    uint32_t code;
    IrProtocol protocol;
};

static const IrCodeEntry kIrCodes[] = {
    // the AC unit
    {"POWER", AC_POWER, IR_PROTO_AC},
    {"PLUS", AC_PLUS, IR_PROTO_AC},
    {"MINUS", AC_MINUS, IR_PROTO_AC},
    {"COUNT_DOWN", AC_COUNT_DOWN, IR_PROTO_AC},
    {"LED", AC_LED, IR_PROTO_AC},
    {"TURBO", AC_TURBO, IR_PROTO_AC},
    {"MODE", AC_MODE, IR_PROTO_AC},
    {"VENTILATOR", AC_VENTILATOR, IR_PROTO_AC},
    {"SLEEP1", AC_SLEEP1, IR_PROTO_AC},
    {"SLEEP2", AC_SLEEP2, IR_PROTO_AC},
    {"SLEEP3", AC_SLEEP3, IR_PROTO_AC},
    // the HY350 Max projector remote, NEC address 0x00
    {"HY350_POWER", HY350_POWER, IR_PROTO_NEC},
    {"HY350_UP", HY350_UP, IR_PROTO_NEC},
    {"HY350_DOWN", HY350_DOWN, IR_PROTO_NEC},
    {"HY350_LEFT", HY350_LEFT, IR_PROTO_NEC},
    {"HY350_RIGHT", HY350_RIGHT, IR_PROTO_NEC},
    {"HY350_OK", HY350_OK, IR_PROTO_NEC},
    {"HY350_VOL_UP", HY350_VOL_UP, IR_PROTO_NEC},
    {"HY350_VOL_DOWN", HY350_VOL_DOWN, IR_PROTO_NEC},
    {"HY350_MUTE", HY350_MUTE, IR_PROTO_NEC},
    {"HY350_HOME", HY350_HOME, IR_PROTO_NEC},
    {"HY350_BACK", HY350_BACK, IR_PROTO_NEC},
    {"HY350_LIST", HY350_LIST, IR_PROTO_NEC},
};

static const size_t kIrCodeCount = sizeof(kIrCodes) / sizeof(kIrCodes[0]);

static const IrCodeEntry *findEntry(uint32_t code)
{
    for (size_t i = 0; i < kIrCodeCount; i++)
        if (kIrCodes[i].code == code)
            return &kIrCodes[i];
    return nullptr;
}

String getIrName(uint32_t IrCode)
{
    const IrCodeEntry *e = findEntry(IrCode);
    return e ? e->name : "UNKNOWN";
}

IrProtocol getIrProtocol(uint32_t code)
{
    const IrCodeEntry *e = findEntry(code);
    return e ? e->protocol : IR_PROTO_NONE;
}

uint32_t getIrCode(const String &name)
{
    String wanted = name;
    wanted.trim();
    wanted.toUpperCase();
    for (size_t i = 0; i < kIrCodeCount; i++)
        if (wanted == kIrCodes[i].name)
            return kIrCodes[i].code;
    return 0; // no real command is 0, so it doubles as "not found"
}

String getIrCodeNames()
{
    String list;
    for (size_t i = 0; i < kIrCodeCount; i++)
    {
        if (i)
            list += ", ";
        list += kIrCodes[i].name;
    }
    return list;
}
