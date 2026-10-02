#include "acGuesser.h"
#include <ArduinoJson.h>
#include <sys/time.h>

AcGuess acGuess;

// The guess as Resources. `AcGuess:acpower` only ever says on or off: while the guess is
// unknown it is withdrawn rather than published as a third value. `AcGuess:Raw` is the evidence
// behind it, a JSON object in a string that consumers decode themselves.
static ManagedSensor<bool> acGuessPowerSensor("AcGuess:acpower");
static ManagedSensor<String> acGuessRawSensor("AcGuess:Raw");

static Snapshot snapshot[50];
static uint8_t snapshotIndex = 0;

// A beep this soon after we transmitted POWER is the unit acknowledging it.
constexpr uint32_t POWER_BEEP_WINDOW_MS = 3000;
// With no beep history, fan and compressor both quiet for this long means off.
constexpr uint32_t IDLE_OFF_MS = 30000;

static void markPowerObserved(bool on)
{
    acGuess.powerKnown = true;
    acGuess.powerGuess = on;
}

AcPowerGuess guessAcPower()
{
    // A running fan or compressor is proof, whatever we think we sent.
    if (acGuess.fanguess || acGuess.compressorguess)
        return AcPowerGuess::ON;
    if (acGuess.powerKnown)
        return acGuess.powerGuess ? AcPowerGuess::ON : AcPowerGuess::OFF;
    // Nothing ever observed: quiet sensors are only evidence if we have heard from them.
    const uint32_t lastActivity = max(acGuess.lastfanchange, acGuess.lastcompressorchange);
    if (lastActivity != 0 && millis() - lastActivity >= IDLE_OFF_MS)
        return AcPowerGuess::OFF;
    return AcPowerGuess::UNKNOWN;
}

// ---- did the unit beep for what we sent? ---------------------------------------------------
// The AC beeps when it accepts a code, so every AC code we transmit should be followed by a beep
// within CONFIRM_WINDOW_MS. Each outcome is published, not retained, on tester/ir/confirmation.
constexpr uint32_t CONFIRM_WINDOW_MS = 10000;
constexpr uint8_t PENDING_MAX = 16;

struct PendingIr
{
    uint32_t code;
    uint32_t sentMs;    // millis(): drives the timeout, immune to clock steps
    uint64_t sentEpoch; // unix ms for the report; 0 while the clock is not valid
    bool report;        // sent by the AC controller: publish its outcome
};

/// Unix time in milliseconds, 0 until the clock is valid. Milliseconds because the unit's beep
/// follows within a second or so, which whole seconds could not tell apart from the send.
static uint64_t unixMs()
{
    if (!NightMare::Time::valid())
        return 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + tv.tv_usec / 1000;
}

// Oldest first: the unit acknowledges in the order it received. Touched from the transmit loop, the
// network task (beeps) and the scheduler task (timeouts), hence the lock.
static PendingIr pendingIr[PENDING_MAX];
static uint8_t pendingCount = 0;
static portMUX_TYPE pendingLock = portMUX_INITIALIZER_UNLOCKED;

/// Fields are exactly: code, firedAt, beepAt, confirmed. Times are unix milliseconds, null while the
/// clock is not valid; beepAt is also null when the window passed without a beep.
static void publishConfirmation(uint32_t code, uint64_t firedAt, bool confirmed, uint64_t beepAt)
{
    JsonDocument doc;
    doc["code"] = getIrName(code);
    if (firedAt != 0)
        doc["firedAt"] = firedAt;
    else
        doc["firedAt"] = nullptr;
    if (confirmed && beepAt != 0)
        doc["beepAt"] = beepAt;
    else
        doc["beepAt"] = nullptr;
    doc["confirmed"] = confirmed;
    String out;
    serializeJson(doc, out);
    NightMare::PublishText("tester/ir/confirmation", out);
}

void acGuessIrSent(uint32_t code, uint32_t sentMs, bool fromAcController)
{
    acGuess.lastIrCode = static_cast<IrCodes>(code);
    acGuess.lastIrSent = sentMs;
    if (getIrProtocol(code) != IR_PROTO_AC)
        return; // the projector does not beep
    bool dropped = false;
    portENTER_CRITICAL(&pendingLock);
    if (pendingCount == PENDING_MAX)
    {
        // A full queue means beeps stopped long ago; the oldest is as good as unconfirmed.
        memmove(&pendingIr[0], &pendingIr[1], sizeof(PendingIr) * (PENDING_MAX - 1));
        pendingCount--;
        dropped = true;
    }
    pendingIr[pendingCount++] = {code, sentMs, unixMs(), fromAcController};
    portEXIT_CRITICAL(&pendingLock);
    if (dropped)
        Serial.println("AcGuess: confirmation queue full, oldest send dropped");
}

/// A beep arrived: it belongs to the oldest send still waiting.
static void confirmOldestPending()
{
    const uint64_t beepEpoch = unixMs();
    PendingIr matched = {0, 0, 0, false};
    bool found = false;
    portENTER_CRITICAL(&pendingLock);
    if (pendingCount > 0)
    {
        matched = pendingIr[0];
        memmove(&pendingIr[0], &pendingIr[1], sizeof(PendingIr) * (pendingCount - 1));
        pendingCount--;
        found = true;
    }
    portEXIT_CRITICAL(&pendingLock);
    if (found && matched.report)
        publishConfirmation(matched.code, matched.sentEpoch, true, beepEpoch);
}

/// Sends that waited the whole window with no beep.
static void expirePending()
{
    for (;;)
    {
        PendingIr expired = {0, 0, 0, false};
        bool found = false;
        portENTER_CRITICAL(&pendingLock);
        if (pendingCount > 0 && millis() - pendingIr[0].sentMs > CONFIRM_WINDOW_MS)
        {
            expired = pendingIr[0];
            memmove(&pendingIr[0], &pendingIr[1], sizeof(PendingIr) * (pendingCount - 1));
            pendingCount--;
            found = true;
        }
        portEXIT_CRITICAL(&pendingLock);
        if (!found)
            return;
        if (expired.report)
            publishConfirmation(expired.code, expired.sentEpoch, false, 0);
    }
}

static const char *powerName(AcPowerGuess guess)
{
    return guess == AcPowerGuess::ON ? "on" : guess == AcPowerGuess::OFF ? "off" : "unknown";
}

static String rawGuessJson(AcPowerGuess guess)
{
    JsonDocument doc;
    doc["power"] = powerName(guess);
    doc["powerKnown"] = acGuess.powerKnown;
    doc["powerGuess"] = acGuess.powerGuess;
    doc["fan"] = acGuess.fanguess;
    doc["compressor"] = acGuess.compressorguess;
    doc["lastBeep"] = acGuess.lastbeep;
    doc["lastFanChange"] = acGuess.lastfanchange;
    doc["lastCompressorChange"] = acGuess.lastcompressorchange;
    doc["lastIrCode"] = getIrName(acGuess.lastIrCode);
    doc["lastIrSent"] = acGuess.lastIrSent;
    String out;
    serializeJson(doc, out);
    return out;
}

/// Re-evaluates the guess and publishes only what changed. Runs periodically, because the guess
/// can change with no new event: "quiet for 30 s" is a conclusion reached by the clock alone.
static void evaluateAcGuess()
{
    static int8_t lastPower = -2; // -1 unknown, 0 off, 1 on
    static String lastRaw;
    const AcPowerGuess guess = guessAcPower();
    const int8_t power = guess == AcPowerGuess::ON ? 1 : guess == AcPowerGuess::OFF ? 0 : -1;
    if (power != lastPower)
    {
        if (power < 0)
            acGuessPowerSensor.setAvailable(false);
        else
        {
            acGuessPowerSensor.setAvailable(true);
            acGuessPowerSensor.setValue(power == 1);
        }
        lastPower = power;
    }
    String raw = rawGuessJson(guess);
    if (raw != lastRaw)
    {
        acGuessRawSensor.setValue(raw);
        lastRaw = raw;
    }
}

void startAcGuess()
{
   NightMare::Subscribe("tester/+");
   NightMare::OnMessage([](const char *topic, const uint8_t *payload, size_t length, bool retained) {
      String topicStr(topic);
      String payloadStr(reinterpret_cast<const char *>(payload), length);
      String type = topicStr.substring(topicStr.lastIndexOf('/') + 1);
      Serial.printf("AC Guess message received: type=%s, value=%s\n", type.c_str(), payloadStr.c_str());
      if (type == "beep")
        {
             const uint32_t now = millis();
             const bool ackOfPower = acGuess.lastIrCode == IrCodes::AC_POWER &&
                                     acGuess.lastIrSent != 0 &&
                                     now - acGuess.lastIrSent <= POWER_BEEP_WINDOW_MS &&
                                     acGuess.lastbeep < acGuess.lastIrSent; // first beep after the send only
             acGuess.lastbeep = now;
             confirmOldestPending();
             if (ackOfPower && acGuess.powerKnown)
                 acGuess.powerGuess = !acGuess.powerGuess;
        }
        else if (type == "compressor")
        {
            acGuess.compressorguess = payloadStr == "1";
            if (acGuess.compressorguess)
                markPowerObserved(true);
            acGuess.lastcompressorchange = millis();
        }
        else if (type == "fan")
        {
            acGuess.fanguess = payloadStr == "1";
            if (acGuess.fanguess)
                markPowerObserved(true);
            acGuess.lastfanchange = millis();
        }       
   });
   gScheduler.everyMonotonic("acGuesser", []() {
      evaluateAcGuess();
      expirePending();
      // Store the current guess state in the snapshot array
      snapshot[snapshotIndex].guess = acGuess;
      snapshot[snapshotIndex].temperature = temperatureSensor.getValue();
      snapshot[snapshotIndex].timestamp = millis();
      snapshotIndex = (snapshotIndex + 1) % 50; // Wrap around to maintain a circular buffer
   }, 1000); // Run every second
}