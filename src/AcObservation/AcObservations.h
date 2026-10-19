#pragma once

#include <NightMareNetwork.h>
#include <AcState/AcCommand.h>
// What Watson hears, as NightMare Remote Resources. These are observations and nothing more: Watson
// says what it hears, IR says what was sent and received, and Adler decides what the AC is doing.
//
// The local names are Adler's own and stable. NightMare owns each Remote source binding: it is
// persisted, restored at startup and changeable with the SOURCE verb. No source device name is
// wired into Adler's detection or control logic.
//
//   ac:input:fan          -> <device>/acoustic:fan_detected          RemoteSensor<bool>
//   ac:input:compressor   -> <device>/acoustic:compressor_detected   RemoteSensor<bool>
//   ac:input:beep         -> <device>/acoustic:beep                  RemoteEvent<std::string>
//
// Freshness, availability and replay are NightMare's: the fan and compressor are state (usable only
// while the Resource has a value, is available and is not stale), the beep is an Event (every
// occurrence matters, nothing is retained, nothing is replayed after a reconnect).
extern RemoteSensor<bool> acInputFan;
extern RemoteSensor<bool> acInputCompressor;
extern RemoteEvent<std::string> acInputBeep;

/// Installs the beep handler. Safe before startNightMareESP().
void installAcObservations();
void setOnBeepHandler(void (*handler)(void));

struct AcObservedState {
    bool fan = false;
    bool compressor = false;
    uint32_t lastFanChange = 0;
    uint32_t lastCompressorChange = 0;
    bool isValid () const;
};

extern AcObservedState gAcObservedState;