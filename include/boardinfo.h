#pragma once
#include <Arduino.h>
#include <board.h>

/* ---------------------------------------------------------------------
 * Adler wiring -- what is physically attached to the unit.
 *
 * board.h owns the pin numbers and every pin-level caveat that goes with them
 * (strapping pins, shared pads, reserved ranges). This file owns what kind of
 * device hangs off each one, which does not change between revisions.
 *
 * Keep it that way: the table takes its GPIOs from board.h's macros rather than
 * repeating them, so only the selected revision reaches flash and there is one
 * place to edit when the unit is re-wired. Do not describe a specific GPIO
 * number here -- that belongs in the revision block in board.h.
 * ------------------------------------------------------------------- */

struct BoardConnection
{
    uint8_t gpio;
    const char *device;
    const char *notes;
};

static constexpr BoardConnection kBoardConnections[] = {
    {PIN_IR_RECEIVE, "IR RX demodulator", "idles HIGH, pulls low on carrier"},
    {PIN_IR_LED, "IR TX LED", "38 kHz carrier from LEDC"},
    {PIN_ONBOARD_LED, "Status LED", "polarity unverified"},
    {PIN_ONE_WIRE, "DS18B20", "OneWire data, 4k7 pull-up to 3V3"},
};

static constexpr size_t kBoardConnectionCount =
    sizeof(kBoardConnections) / sizeof(kBoardConnections[0]);

/// @brief What the wiring table says is attached to a GPIO, or nullptr when the pin
/// is not one of ours. Lets a debug command warn before it fights a live peripheral.
inline const char *boardDeviceOnPin(uint8_t gpio)
{
    for (size_t i = 0; i < kBoardConnectionCount; i++)
    {
        if (kBoardConnections[i].gpio == gpio)
            return kBoardConnections[i].device;
    }
    return nullptr;
}

/// @brief The wiring table as JSON, including the level each pin reads right now.
inline String BoardInfoJson()
{
    String json = "{\"board\":\"" BOARD_NAME "\",\"connections\":[";
    for (size_t i = 0; i < kBoardConnectionCount; i++)
    {
        if (i)
            json += ",";
        json += "{\"gpio\":" + String(kBoardConnections[i].gpio);
        json += ",\"device\":\"" + String(kBoardConnections[i].device) + "\"";
        json += ",\"level\":" + String(digitalRead(kBoardConnections[i].gpio));
        json += ",\"notes\":\"" + String(kBoardConnections[i].notes) + "\"}";
    }
    json += "]}";
    return json;
}

/// @brief Prints the wiring table. The live level is what makes this worth printing
/// at boot: it separates "the firmware is looking at the wrong pin" from "the pin is
/// right but nothing is driving it".
inline void printBoardInfo()
{
    Serial.println("Board: " BOARD_NAME);
    for (size_t i = 0; i < kBoardConnectionCount; i++)
    {
        Serial.printf("\tGPIO%-2u %-18s level=%d  %s\n",
                      kBoardConnections[i].gpio,
                      kBoardConnections[i].device,
                      digitalRead(kBoardConnections[i].gpio),
                      kBoardConnections[i].notes);
    }
}
