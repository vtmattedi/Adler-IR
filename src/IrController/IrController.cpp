#include "IrController.h"
#include <ArduinoJson.h>
#include <AcState/AcCommand.h>
// IRremote's feedback LED defaults to LED_BUILTIN, which is GPIO8 here: the same pin the
// LEDC carrier owns. Every mark() then digitalWrite()s a pin that is not in GPIO mode.
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>

static const char *irTag = "\x1b[31mIR\x1b[0m";

#if BOARD_HAS_IR_RECEIVER
static ManagedEvent<std::string> irRecvSensor("ir:recv");
#endif

static const ActionArgMetadata kIrSendArguments[] = {
    {"code", NetValueType::STRING},
};
static ManagedAction irSendAction("ir:send", kIrSendArguments);

static bool parseHexCode(const std::string &text, IrCodes &code)
{
    std::string candidate = text;
    NightMare::Text::trim(candidate);
    if (NightMare::Text::startsWith(candidate, "0x") || NightMare::Text::startsWith(candidate, "0X"))
        candidate.erase(0, 2);
    if (candidate.empty() || candidate.length() > 8)
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
    code = static_cast<IrCodes>(parsed);
    return true;
}

static ActionResult invokeIrSend(ManagedAction &, const std::string &payload)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload) || !doc.is<JsonObjectConst>())
        return {false, "expected {\"code\":\"POWER\"}"};

    JsonVariantConst value = doc["code"];
    if (!value.is<const char *>())
        return {false, "'code' must be a string"};

    const std::string requested = value.as<std::string>();
    IrCodes code = getIrCodeByName(requested);
    if (code == IrCodes::NO_CODE && (!parseHexCode(requested, code) || getIrProtocol(code) == IR_PROTO_NONE))
        return {false, "unknown IR code; use a known name or hexadecimal value"};
    if (code == IrCodes::NO_CODE)
        return {false, "unknown IR code; use a known name or hexadecimal value"};
    if (!sendIRCode(static_cast<IrCodes>(code)))
        return {false, "IR command is busy or the transmit queue is full"};

    return {true, "queued " + getIrName(code)};
}

static QueueHandle_t txQueue = nullptr;

static portMUX_TYPE lastSentLock = portMUX_INITIALIZER_UNLOCKED;

uint32_t sendIRCode(IrCodes code)
{
    static uint32_t nextId = 1;
    if (getIrProtocol(code) == IR_PROTO_NONE)
    {
        LOG_ERROR(irTag, " refusing to queue 0x%08lX: not in the table", (unsigned long)code);
        return 0;
    }
    IrSendPayload payload;
    payload.code = code;
    payload.userId = nextId++;
    if (nextId == 0)
        nextId = 1; // wrap around, but never return 0
    if (txQueue == nullptr || xQueueSend(txQueue, &payload, 100) != pdTRUE)
    {
        LOG_ERROR(irTag, " queue unavailable or full, dropped %s", getIrName(code).c_str());
        return 0;
    }
    return true;
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

/// Sends queued codes one at a time. AC frames are handed to AcCommand once they have left the LED.
static void txConsumerTask(void *)
{
    for (;;)
    {
        uint32_t code = 0;
        if (xQueueReceive(txQueue, &code, portMAX_DELAY) != pdTRUE)
            continue;

        LOG(irTag, " tx %s (0x%08lX) on GPIO%u", getIrName(code).c_str(), (unsigned long)code, IR_SEND_PIN);
        transmit(code);
        if (getIrProtocol(code) == IR_PROTO_AC)
            gAcCommandController.irDetected(static_cast<IrCodes>(code), Source::SENT);

        // The unit needs time to act on one code before the next.
        vTaskDelay(pdMS_TO_TICKS(IR_SEND_INTERVAL_MS));
    }
}

// ---- receive ---------------------------------------------------------------------

#if BOARD_HAS_IR_RECEIVER

/// Reports one decoded frame to the network and, if it is a command for the AC, to AcCommand.
static void handleFrame(decode_type_t protocol, uint8_t bits, uint32_t raw, bool isRepeat)
{
    // A frame is ours only when protocol and raw both match: the AC's frames decode as
    // PULSE_DISTANCE, the projector remote's as NEC.
    const IrProtocol table = getIrProtocol(raw);
    const bool acFrame = protocol == PULSE_DISTANCE && table == IR_PROTO_AC;
    const bool necFrame = protocol == NEC && table == IR_PROTO_NEC;
    if (!acFrame && !necFrame && protocol == UNKNOWN)
        return; // noise

    LOG(irTag, " rx protocol=%s raw=0x%08lX bits=%u -> %s", getProtocolString(protocol),
        (unsigned long)raw, bits, getIrName(raw).c_str());

    // Hearing the AC remote proves nothing about what the unit did; AcCommand waits for its beep.
    if (acFrame)
        gAcCommandController.irDetected(static_cast<IrCodes>(raw), Source::PHYSICAL_REMOTE);


    // Report the frame to the network, even if it is not one of ours. The payload is a JSON object
    irRecvSensor.fire(std::string("{\"protocol\":\"") + getProtocolString(protocol) + "\",\"bits\":" +
                      std::to_string(bits) + ",\"raw\":\"0x" + NightMare::Text::hex(raw) + "\"}");
}

static void rxTask(void *)
{
    IrReceiver.begin(IR_RECEIVE_PIN);
    LOG(irTag, " receiver listening on GPIO%u", IR_RECEIVE_PIN);
    for (;;)
    {
        if (IrReceiver.decode())
        {
            const auto &data = IrReceiver.decodedIRData;
            // we only process frames that decode to a known protocol and have non-zero raw data; the rest is noise
            if (data.decodedRawData != 0 && data.protocol != UNKNOWN && !(data.flags & IRDATA_FLAGS_IS_REPEAT))
            handleFrame(data.protocol, data.numberOfBits, data.decodedRawData,
                        (data.flags & IRDATA_FLAGS_IS_REPEAT) != 0);
            IrReceiver.resume();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif // BOARD_HAS_IR_RECEIVER

// ---- lifecycle -------------------------------------------------------------------

void startIrServices()
{
    irSendAction.onInvoke = invokeIrSend;
    pinMode(IR_SEND_PIN, OUTPUT);
    IrSender.begin(IR_SEND_PIN);

    txQueue = xQueueCreate(IR_ASYNC_QUEUE_SIZE, sizeof(IrSendPayload));
    if (txQueue == nullptr ||
        xTaskCreate(txConsumerTask, "ir_tx", IR_TASK_STACK_SIZE, nullptr, IR_TASK_PRIORITY, nullptr) != pdPASS)
    {
        LOG_ERROR(irTag, "failed to start the transmit task");
        return;
    }
  
#if BOARD_HAS_IR_RECEIVER
    pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);
    LOG(irTag, " rx GPIO%u idle level %d %s", IR_RECEIVE_PIN, digitalRead(IR_RECEIVE_PIN),
        digitalRead(IR_RECEIVE_PIN) ? "(expected)" : "(SUSPECT: check receiver power and wiring)");
    if (xTaskCreate(rxTask, "ir_rx", IR_TASK_STACK_SIZE, nullptr, IR_TASK_PRIORITY, nullptr) != pdPASS)
        LOG_ERROR(irTag, "failed to start the receive task");
#endif
}
