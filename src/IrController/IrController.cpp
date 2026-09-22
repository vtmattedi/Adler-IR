#include "IrController.h"
#include <ArduinoJson.h>
#include <IRremote.hpp>
#include <NightMare/NetResources.h>

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

// ---- belief about the AC unit ---------------------------------------------------

AcIrStateController::AcIrStateController()
{
    state.power = false;
    state.temp = MIN_AC_TEMP;
    state.mode = AC_MODE_COOL;
    state.fan = 0;
    state.led = false;
    state.turbo = false;
}

void AcIrStateController::nextState(IrCodes code)
{
    // Only the AC's own codes move the AC's belief. A projector code through here is a no-op.
    if (getIrProtocol(code) != IR_PROTO_AC)
        return;
    if (code == AC_POWER)
    {
        state.power = !state.power;
        state.mode = AC_MODE_COOL;
        state.turbo = false;
        return;
    }
    if (!state.power)
    {
        // If the AC is off, only POWER has an effect
        return;
    }
    // in turbo mode, +/- do not change the temperature
    if (code == AC_PLUS && !state.turbo)
    {
        if (state.temp < MAX_AC_TEMP)
            state.temp++;
    }
    else if (code == AC_MINUS && !state.turbo)
    {
        if (state.temp > MIN_AC_TEMP)
            state.temp--;
    }
    else if (code == AC_LED)
    {
        state.led = !state.led;
    }
    else if (code == AC_TURBO)
    {
        state.turbo = !state.turbo;
        state.mode = AC_MODE_COOL;
        if (state.turbo)
        {
            state.temp = MIN_AC_TEMP;
            state.fan = 1;
        }
    }
    else if (code == AC_MODE)
    {
        if (state.turbo)
            state.turbo = false;
        else
            state.mode = static_cast<AcIrMode>((state.mode + 1) % 3);
    }
    else if (code == AC_VENTILATOR)
    {
        state.fan = state.fan ? 0 : 1;
    }
}

bool AcIrStateController::setPower(bool powerState)
{
    if (state.power == powerState)
        return true;
    // No direct assignment: sendIRCode() applies nextState(), which performs the
    // toggle. Setting state.power here as well would cancel that toggle out.
    return sendIRCode(AC_POWER);
}

bool AcIrStateController::setTemp(uint8_t temp)
{
    if (temp < MIN_AC_TEMP || temp > MAX_AC_TEMP)
        return false;
    if (state.temp == temp)
        return true;
    // Everything is queued now and transmitted later, IR_SEND_INTERVAL_MS apart. nextState()
    // runs at queue time, so the loop below converges on the tracked state immediately and
    // never waits on the transmitter -- which is what used to hold loop() for seconds.
    bool wasOn = state.power;
    if (!wasOn && !sendIRCode(AC_POWER))
        return false;
    bool complete = true;
    uint8_t guard = MAX_AC_TEMP - MIN_AC_TEMP + 1;
    while (state.temp != temp && guard--)
    {
        if (!sendIRCode(state.temp < temp ? AC_PLUS : AC_MINUS))
        {
            complete = false;
            break; // The belief stays consistent with what was actually queued.
        }
    }
    if (!wasOn && !sendIRCode(AC_POWER))
        complete = false;
    return complete && state.temp == temp && state.power == wasOn;
}

bool AcIrStateController::setMode(AcIrMode mode)
{
    if (mode < AC_MODE_COOL || mode > AC_MODE_HUMIDIFIER)
        return false;
    if (this->state.mode == mode)
        return true;
    if (!state.power)
        return false;
    // MODE cycles cool -> ventilator -> humidifier -> cool. Press it until the belief matches.
    uint8_t guard = 3;
    while (this->state.mode != mode && guard--)
        if (!sendIRCode(AC_MODE))
            return false;
    return this->state.mode == mode;
}

void AcIrStateController::manualSync(bool power, uint8_t temp)
{
    state.power = power;
    if (temp >= MIN_AC_TEMP && temp <= MAX_AC_TEMP)
        state.temp = temp;
    state.turbo = false;
    state.mode = AC_MODE_COOL;
    known = true;
}

void AcIrStateController::printState()
{
    Serial.println("Current AC belief:");
    Serial.println("Power: " + String(state.power ? "ON" : "OFF"));
    Serial.println("Temperature: " + String(state.temp) + "\xC2\xB0" "C");
    Serial.println("Mode: " + String(state.mode == AC_MODE_COOL ? "COOL" : state.mode == AC_MODE_VENTILATOR ? "VENTILATOR"
                                                                                                             : "HUMIDIFIER"));
    Serial.println("Fan: " + String(state.fan == 0 ? "LOW" : "HIGH"));
    Serial.println("Turbo: " + String(state.turbo ? "ON" : "OFF"));
    Serial.println("LED: " + String(state.led ? "ON" : "OFF"));
}

String AcIrStateController::toJson()
{
    DynamicJsonDocument doc(256);
    doc["power"] = state.power;
    doc["temp"] = state.temp;
    doc["mode"] = state.mode;
    doc["fan"] = state.fan;
    doc["turbo"] = state.turbo;
    doc["led"] = state.led;
    doc["known"] = known;
    String json;
    serializeJson(doc, json);
    return json;
}

AcIrStateController acIrState;
bool irDebugEnabled = false;

void enableIrDebug(bool enable)
{
    irDebugEnabled = enable;
    PersistentSettings.setFlag("ir_debug", enable);
}

bool getIrDebug()
{
    return irDebugEnabled;
}

// ---- transmit queue --------------------------------------------------------------

static uint32_t _irQueue[IR_ASYNC_QUEUE_SIZE] = {0};
static uint8_t _irQueueWrite = 0;
static uint8_t _irQueueReadIndex = 0;

/// Shared between the pump (loop task) and the receive task: what we last sent and when, so
/// the receiver can tell our own echo from a remote.
static portMUX_TYPE irLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastSentCode = 0;
static uint32_t lastSentMs = 0;

bool sendIRCode(uint32_t code)
{
    if (getIrProtocol(code) == IR_PROTO_NONE)
    {
        if (irDebugEnabled)
            Serial.printf("[IR] refusing to queue 0x%08lX: not in the table\n", (unsigned long)code);
        return false;
    }
    noInterrupts();
    bool isQueueFull = _irQueue[_irQueueWrite] != 0;
    if (isQueueFull)
    {
        interrupts();
        if (irDebugEnabled)
            Serial.println("[IR] queue full, dropped " + getIrName(code));
        return false;
    }
    _irQueue[_irQueueWrite] = code;
    _irQueueWrite = (_irQueueWrite + 1) % IR_ASYNC_QUEUE_SIZE;
    // The AC belief is advanced here, at queue time, and nowhere else on the send path:
    // callers reason about it right after queueing, not after the pump gets to it.
    acIrState.nextState(static_cast<IrCodes>(code));
    interrupts();
    return true;
}

static uint8_t irQueueDepth()
{
    uint8_t n = 0;
    noInterrupts();
    for (uint8_t i = 0; i < IR_ASYNC_QUEUE_SIZE; i++)
        if (_irQueue[i] != 0)
            n++;
    interrupts();
    return n;
}

/// @brief Transmits one code the way its protocol requires.
static void transmit(uint32_t code)
{
    switch (getIrProtocol(code))
    {
    case IR_PROTO_AC:
        IrSender.sendPulseDistanceWidth(38, 9000, 4550, 600, 1700, 600, 550, code, 24, PROTOCOL_IS_LSB_FIRST, 100, 1);
        break;
    case IR_PROTO_NEC:
        // Raw is ~cmd|cmd|~addr|addr, LSB first: the address is the low byte, the command byte 2.
        IrSender.sendNEC((uint16_t)(code & 0xFF), (uint8_t)((code >> 16) & 0xFF), 0);
        break;
    default:
        break; // sendIRCode() never queues these
    }
}

static void handleIrAsync()
{
    static unsigned long lastIrSendTime = 0;
    uint32_t codeToSend = 0;
    if (millis() - lastIrSendTime < IR_SEND_INTERVAL_MS)
        return;
    noInterrupts();
    if (_irQueue[_irQueueReadIndex] != 0)
    {
        codeToSend = _irQueue[_irQueueReadIndex];
        _irQueue[_irQueueReadIndex] = 0;
        _irQueueReadIndex = (_irQueueReadIndex + 1) % IR_ASYNC_QUEUE_SIZE;
    }
    interrupts();
    if (codeToSend == 0)
        return;

    // Logged before the call: if the transmit ever blocks or faults, the last line on the
    // console still says which code was being sent.
    Serial.printf("[IR] tx %s (0x%08lX, %s) on GPIO%u\n", getIrName(codeToSend).c_str(), (unsigned long)codeToSend,
                  getIrProtocol(codeToSend) == IR_PROTO_NEC ? "NEC" : "AC", IR_SEND_PIN);

    lastIrSendTime = millis();
    portENTER_CRITICAL(&irLock);
    lastSentCode = codeToSend;
    lastSentMs = lastIrSendTime;
    portEXIT_CRITICAL(&irLock);

    transmit(codeToSend);

    if (irDebugEnabled)
        Serial.printf("[IR] tx done, AC belief %s\n", acIrState.toJson().c_str());
}

// ---- receiver as a sensor --------------------------------------------------------

#if BOARD_HAS_IR_RECEIVER
static portMUX_TYPE sensorLock = portMUX_INITIALIZER_UNLOCKED;
static IrSensorStatus irSensor = {false, 0, "", 0};

IrSensorStatus irSensorStatus()
{
    portENTER_CRITICAL(&sensorLock);
    IrSensorStatus copy = irSensor;
    portEXIT_CRITICAL(&sensorLock);
    return copy;
}

static void recordReceived(uint32_t code, const String &name)
{
    portENTER_CRITICAL(&sensorLock);
    irSensor.everReceived = true;
    irSensor.code = code;
    strncpy(irSensor.name, name.c_str(), sizeof(irSensor.name) - 1);
    irSensor.name[sizeof(irSensor.name) - 1] = '\0';
    irSensor.lastReceiveMs = millis();
    portEXIT_CRITICAL(&sensorLock);

    irRecvSensor.setValue(name);
}

/// @brief Whether a decoded frame is our own transmission coming back off the room.
static bool isSelfEcho(uint32_t code)
{
    portENTER_CRITICAL(&irLock);
    uint32_t sent = lastSentCode;
    uint32_t sentMs = lastSentMs;
    portEXIT_CRITICAL(&irLock);
    return sent == code && sentMs && (millis() - sentMs) < IR_SELF_ECHO_WINDOW_MS;
}

static void IRReceiveHandler(void *pvParameters)
{
    IrReceiver.begin(IR_RECEIVE_PIN);
    Serial.printf("[IR] receiver listening on GPIO%u\n", IR_RECEIVE_PIN);
    unsigned long lastReceiveTime = 0;
    uint32_t lastReceivedCode = 0;
    for (;;)
    {
        if (IrReceiver.decode())
        {
            const decode_type_t protocol = IrReceiver.decodedIRData.protocol;
            const uint32_t raw = IrReceiver.decodedIRData.decodedRawData;
            if (raw == 0 && protocol == UNKNOWN)
            {
                // The library's decode() returns a 0/UNKNOWN pair when it has no more data to
                // give. It does not mean the remote sent a 0 code, which is not a real code.
                IrReceiver.resume();
                continue;
            }
            const bool isRepeat = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
            const IrProtocol table = getIrProtocol(raw);
            // A frame is ours only when protocol and raw both match: the AC's frames decode as
            // PULSE_DISTANCE, the projector remote's as NEC.
            const bool acFrame = protocol == PULSE_DISTANCE && table == IR_PROTO_AC;
            const bool necFrame = protocol == NEC && table == IR_PROTO_NEC;
            const String name = getIrName(raw);

            // Every decode is logged, whatever it is: a remote arriving under a protocol the
            // table does not list looks exactly like silence otherwise.
            Serial.printf("[IR] rx protocol=%s raw=0x%08lX bits=%u repeat=%d -> %s\n",
                          getProtocolString(protocol), (unsigned long)raw,
                          IrReceiver.decodedIRData.numberOfBits, isRepeat, name.c_str());
            if (irDebugEnabled)
                IrReceiver.printIRResultShort(&Serial);

            if (isRepeat)
            {
                // Held button: the first frame was already counted.
            }
            else if (isSelfEcho(raw))
            {
                // Our own LED. Any belief change happened when the code was queued.
                Serial.println("[IR] rx is our own transmission, ignored");
            }
            else if (raw == lastReceivedCode && millis() - lastReceiveTime < 200)
            {
                Serial.println("[IR] rx suppressed as a bounce of the previous code");
            }
            else if (acFrame)
            {
                // Someone pressed the AC remote: the unit acted on it, so the belief follows,
                // and a press we can decode is proof the unit's state is now known.
                acIrState.nextState(static_cast<IrCodes>(raw));
                acIrState.known = true;
                recordReceived(raw, name);
                Serial.printf("[IR] rx applied, AC belief %s\n", acIrState.toJson().c_str());
                lastReceivedCode = raw;
                lastReceiveTime = millis();
            }
            else if (necFrame)
            {
                // The projector remote. Reported, not acted on: this device does not control it.
                recordReceived(raw, name);
                lastReceivedCode = raw;
                lastReceiveTime = millis();
            }
            else if (protocol != UNKNOWN)
            {
                // A remote we do not know, under a protocol we could decode. Report it tagged
                // with the protocol so it can be identified from the readings and added.
                recordReceived(raw, String(getProtocolString(protocol)) + ":0x" + String(raw, HEX));
                lastReceivedCode = raw;
                lastReceiveTime = millis();
            }
            IrReceiver.resume();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#endif // BOARD_HAS_IR_RECEIVER

String IrInfoJson()
{
    String json = "{";
    json += "\"sendPin\": " + String(IR_SEND_PIN) + ",";
#if BOARD_HAS_IR_RECEIVER
    json += "\"receivePin\": " + String(IR_RECEIVE_PIN) + ",";
    json += "\"receivePinLevel\": " + String(digitalRead(IR_RECEIVE_PIN)) + ",";
#else
    json += "\"receivePin\": null,";
    json += "\"receivePinLevel\": null,";
#endif
    json += "\"debug\": " + String(irDebugEnabled ? "true" : "false") + ",";
    json += "\"queued\": " + String(irQueueDepth()) + ",";
    json += "\"acState\": " + acIrState.toJson();
    json += "}";
    return json;
}

void startIrServices()
{
    pinMode(IR_SEND_PIN, OUTPUT);
#if BOARD_HAS_IR_RECEIVER
    pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);
#endif
    enableIrDebug(PersistentSettings.getFlag("ir_debug"));
    IrSender.begin(IR_SEND_PIN);
#if BOARD_HAS_IR_RECEIVER
    Serial.printf("[IR] tx GPIO%u, rx GPIO%u, debug %s\n", IR_SEND_PIN, IR_RECEIVE_PIN, irDebugEnabled ? "ON" : "OFF");
    Serial.printf("[IR] rx pin idle level %d %s\n", digitalRead(IR_RECEIVE_PIN),
                  digitalRead(IR_RECEIVE_PIN) ? "(expected)" : "(SUSPECT: check receiver power and wiring)");
    BaseType_t res = xTaskCreate(IRReceiveHandler, "ir_receive", IR_RECEIVE_TASK_STACK_SIZE, NULL, IR_RECEIVE_TASK_PRIORITY, NULL);
    Serial.printf("IR receive task created: %s\n", res == pdPASS ? "yes" : "no");
#else
    Serial.printf("[IR] tx GPIO%u, no receiver, debug %s\n", IR_SEND_PIN, irDebugEnabled ? "ON" : "OFF");
#endif
}

void pumpIrServices()
{
    handleIrAsync();
}
