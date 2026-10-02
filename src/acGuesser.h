#pragma once
#include <NightMare.h>
#include <IrController/IrCodes.h>
#include <TempSensor/TempSensor.h>

struct AcGuess
{
    uint32_t lastbeep = 0;
    uint32_t lastIrSent = 0;
    IrCodes lastIrCode = IrCodes::AC_POWER;
    bool fanguess = false;
    bool compressorguess = false;
    uint32_t lastcompressorchange = 0;
    uint32_t lastfanchange = 0;
    // Is the unit powered? Only meaningful once powerKnown: set by the first sign of
    // life (fan or compressor running), then flipped by every POWER we sent that the
    // unit acknowledged with a beep. Not derived from what we *queued*.
    bool powerKnown = false;
    bool powerGuess = false;
};

enum class AcPowerGuess : uint8_t
{
    UNKNOWN,
    OFF,
    ON,
};

/// @brief Best guess of whether the physical unit is on, from its fan, compressor and beeps.
AcPowerGuess guessAcPower();

struct Snapshot
{
    AcGuess guess;
    float temperature = 0.0f;
    uint32_t timestamp = 0;
};

// One shared instance: defined in acGuess.cpp. (`static` here gave every including file its own copy.)
extern AcGuess acGuess;
/// @brief Called by the IR transmitter right after a code goes out. Records it as the last send and,
/// for an AC code, starts waiting for the unit's beep. Every AC code is tracked, since every one
/// beeps and an unmatched send would shift the order; only `fromAcController` ones are reported on
/// `tester/ir/confirmation`.
void acGuessIrSent(uint32_t code, uint32_t sentMs, bool fromAcController);
void startAcGuess();
