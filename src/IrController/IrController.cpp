#include "IrController.h"
#include <acGuesser.h>
#include <ArduinoJson.h>
// IRremote's feedback LED defaults to LED_BUILTIN, which is GPIO8 here: the same pin the
// LEDC carrier owns. Every mark() then digitalWrite()s a pin that is not in GPIO mode.
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>

// ---- the network surface -------------------------------------------------------
// IrController owns its Resources, stable names, handlers; NightMare binds them at startup (see startIrServices()).

static Config<bool> irDebugConfig("ir:debug:enable", false);
#if BOARD_HAS_IR_RECEIVER
/// A generic struct is a JSON object carried as a String: the wire type stays `string` and the
/// consumer decodes it. Fields: protocol, name, raw ("0x........"), bits, known, ms.
static ManagedSensor<String> irRecvSensor("ir:recv");
#endif

static const ActionArgMetadata kIrSendArguments[] = {
    {"code", NetValueType::STRING},
};
static ManagedAction irSendAction("ir:send", kIrSendArguments);

static bool parseHexCode(const String &text, uint32_t &code)
{
    String candidate = text;
    candidate.trim();
    if (candidate.startsWith("0x") || candidate.startsWith("0X"))
        candidate.remove(0, 2);
    if (candidate.length() == 0 || candidate.length() > 8)
        return false;

    uint32_t parsed = 0;
    for (size_t i = 0; i < candidate.length(); ++i)
    {
        const char c = candidate[i];
        uint8_t digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint8_t>(c - 'A' + 10);
        else
            return false;
        parsed = (parsed << 4) | digit;
    }
    code = parsed;
    return true;
}

static ActionResult invokeIrSend(ManagedAction &, const String &payload)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
        return {false, "expected {\"code\":\"POWER\"}"};

    JsonVariantConst value = doc["code"];
    if (!value.is<const char *>())
        return {false, "'code' must be a string"};

    const String requested = value.as<String>();
    uint32_t code = getIrCode(requested);
    if (code == 0 && (!parseHexCode(requested, code) || getIrProtocol(code) == IR_PROTO_NONE))
        return {false, "unknown IR code; use a known name or hexadecimal value"};
    if (!sendIRCode(code))
        return {false, "IR transmit queue is full"};

    return {true, String("queued ") + getIrName(code)};
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
    return sendIRCode(AC_POWER, IR_SRC_AC_CONTROLLER);
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
    if (!wasOn && !sendIRCode(AC_POWER, IR_SRC_AC_CONTROLLER))
        return false;
    bool complete = true;
    uint8_t guard = MAX_AC_TEMP - MIN_AC_TEMP + 1;
    while (state.temp != temp && guard--)
    {
        if (!sendIRCode(state.temp < temp ? AC_PLUS : AC_MINUS, IR_SRC_AC_CONTROLLER))
        {
            complete = false;
            break; // The belief stays consistent with what was actually queued.
        }
    }
    if (!wasOn && !sendIRCode(AC_POWER, IR_SRC_AC_CONTROLLER))
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
        if (!sendIRCode(AC_MODE, IR_SRC_AC_CONTROLLER))
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
    JsonDocument doc;
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
void enableIrDebug(bool enable)
{
    irDebugConfig.set(enable);
}

bool getIrDebug()
{
    return irDebugConfig.value();
}

// ---- transmit queue --------------------------------------------------------------

static uint32_t _irQueue[IR_ASYNC_QUEUE_SIZE] = {0};
static uint8_t _irQueueSource[IR_ASYNC_QUEUE_SIZE] = {0}; // IrSource, parallel to _irQueue
static uint8_t _irQueueWrite = 0;
static uint8_t _irQueueReadIndex = 0;

/// Shared between the pump (loop task) and the receive task: what we last sent and when, so
/// the receiver can tell our own echo from a remote.
static portMUX_TYPE irLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastSentCode = 0;
static uint32_t lastSentMs = 0;

bool sendIRCode(uint32_t code, IrSource source)
{
    if (getIrProtocol(code) == IR_PROTO_NONE)
    {
        if (getIrDebug())
            Serial.printf("[IR] refusing to queue 0x%08lX: not in the table\n", (unsigned long)code);
        return false;
    }
    noInterrupts();
    bool isQueueFull = _irQueue[_irQueueWrite] != 0;
    if (isQueueFull)
    {
        interrupts();
        if (getIrDebug())
            Serial.println("[IR] queue full, dropped " + getIrName(code));
        return false;
    }
    _irQueue[_irQueueWrite] = code;
    _irQueueSource[_irQueueWrite] = source;
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
    IrSource sourceOfCode = IR_SRC_DIRECT;
    if (millis() - lastIrSendTime < IR_SEND_INTERVAL_MS)
        return;
    noInterrupts();
    if (_irQueue[_irQueueReadIndex] != 0)
    {
        codeToSend = _irQueue[_irQueueReadIndex];
        sourceOfCode = static_cast<IrSource>(_irQueueSource[_irQueueReadIndex]);
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
    NightMare::PublishText("tester/sent", getIrName(codeToSend));
    acGuessIrSent(codeToSend, lastIrSendTime, sourceOfCode == IR_SRC_AC_CONTROLLER);
    if (getIrDebug())
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

static void recordReceived(decode_type_t protocol, uint8_t bits, uint32_t code, const String &name, bool known)
{
    portENTER_CRITICAL(&sensorLock);
    irSensor.everReceived = true;
    irSensor.code = code;
    strncpy(irSensor.name, name.c_str(), sizeof(irSensor.name) - 1);
    irSensor.name[sizeof(irSensor.name) - 1] = '\0';
    irSensor.lastReceiveMs = millis();
    portEXIT_CRITICAL(&sensorLock);

    JsonDocument doc;
    doc["protocol"] = getProtocolString(protocol);
    doc["name"] = name;
    char rawText[11];
    snprintf(rawText, sizeof(rawText), "0x%08lX", (unsigned long)code);
    doc["raw"] = rawText;
    doc["bits"] = bits;
    doc["known"] = known;
    doc["ms"] = millis(); // also makes two identical presses distinct readings
    String frame;
    serializeJson(doc, frame);
    irRecvSensor.setValue(frame);
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
            auto delta = millis() - lastReceiveTime;
            const decode_type_t protocol = IrReceiver.decodedIRData.protocol;
            const uint32_t raw = IrReceiver.decodedIRData.decodedRawData;
            if (raw == 0 || protocol == UNKNOWN)
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
            Serial.printf("[IR] rx protocol=%s raw=0x%08lX bits=%u repeat=%d -> %s (delta: %lu ms)\n",
                          getProtocolString(protocol), (unsigned long)raw,
                          IrReceiver.decodedIRData.numberOfBits, isRepeat, name.c_str(), delta);
            if (getIrDebug())
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
                recordReceived(protocol, IrReceiver.decodedIRData.numberOfBits, raw, name, true);
                Serial.printf("[IR] rx applied, AC belief %s\n", acIrState.toJson().c_str());
                lastReceivedCode = raw;
                lastReceiveTime = millis();
            }
            else if (necFrame)
            {
                // The projector remote. Reported, not acted on: this device does not control it.
                recordReceived(protocol, IrReceiver.decodedIRData.numberOfBits, raw, name, true);
                lastReceivedCode = raw;
                lastReceiveTime = millis();
            }
            else if (protocol != UNKNOWN)
            {
                // A remote we do not know, under a protocol we could decode. Report it tagged
                // with the protocol so it can be identified from the readings and added.
                recordReceived(protocol, IrReceiver.decodedIRData.numberOfBits, raw, name, false);
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
    json += "\"debug\": " + String(getIrDebug() ? "true" : "false") + ",";
    json += "\"queued\": " + String(irQueueDepth()) + ",";
    json += "\"acState\": " + acIrState.toJson();
    json += "}";
    return json;
}

void startIrServices()
{
    irSendAction.onInvoke = invokeIrSend;
    pinMode(IR_SEND_PIN, OUTPUT);
    Serial.printf("[IR] services initialized\n");
#if BOARD_HAS_IR_RECEIVER
    pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);
#endif
    IrSender.begin(IR_SEND_PIN);
#if BOARD_HAS_IR_RECEIVER
    Serial.printf("[IR] tx GPIO%u, rx GPIO%u, debug %s\n", IR_SEND_PIN, IR_RECEIVE_PIN, getIrDebug() ? "ON" : "OFF");
    Serial.printf("[IR] rx pin idle level %d %s\n", digitalRead(IR_RECEIVE_PIN),
                  digitalRead(IR_RECEIVE_PIN) ? "(expected)" : "(SUSPECT: check receiver power and wiring)");
    BaseType_t res = xTaskCreate(IRReceiveHandler, "ir_receive", IR_RECEIVE_TASK_STACK_SIZE, NULL, IR_RECEIVE_TASK_PRIORITY, NULL);
    Serial.printf("IR receive task created: %s\n", res == pdPASS ? "yes" : "no");
#else
    Serial.printf("[IR] tx GPIO%u, no receiver, debug %s\n", IR_SEND_PIN, getIrDebug() ? "ON" : "OFF");
#endif
}

void pumpIrServices()
{
    handleIrAsync();
}
