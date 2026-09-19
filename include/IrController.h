#pragma once
#include <Arduino.h>
#include <board.h>

// The IR side of Adler is two things on one pair of pins:
//   an actuator -- codes queued by sendIRCode() and transmitted by a pump. Codes are
//   generic: the table knows each one's protocol. The AC's own codes also advance
//   the state the AC unit is believed to be in (AcIrStateController) as they are queued;
//   a receiver  -- decoded in the main Runtime and exposed as a Value and Event.

#define MIN_AC_TEMP 18
#define MAX_AC_TEMP 30
// Deep enough for a full 18->30 walk plus power on either side. setTemp() queues
// every step at once and returns; the pump spaces them.
#define IR_ASYNC_QUEUE_SIZE 32
// Gap between transmitted codes. The unit needs time to act on one before the next.
#define IR_SEND_INTERVAL_MS 200
// A code the receiver hears this soon after we transmitted the same code is our own
// LED bouncing off the wall, not the remote.
#define IR_SELF_ECHO_WINDOW_MS 400

// Both are constants rather than macros on purpose. IRSend.hpp does
//     #if defined(IR_SEND_PIN)
//     #define sendPin IR_SEND_PIN
// so a macro named IR_SEND_PIN would replace IrSender's runtime sendPin member
// and quietly ignore the pin handed to IrSender.begin(). IR_RECEIVE_PIN carries
// no such meaning for IrReceiver, but is kept symmetrical with it.
constexpr uint8_t IR_SEND_PIN = PIN_IR_LED;
constexpr uint8_t IR_RECEIVE_PIN = PIN_IR_RECEIVE;

/// @brief Every IR code this device knows, across remotes. The table in IrController.cpp
/// pairs each with its wire name and protocol.
enum IrCodes
{
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
String getIrName(uint32_t IrCode);
/// @brief Looks up a code by its wire name, the inverse of getIrName. Case and surrounding
/// whitespace are ignored.
/// @return The code, or 0 when the name is not one of the known commands.
uint32_t getIrCode(const String &name);
/// @brief Every name getIrCode() accepts, comma separated, for help text.
String getIrCodeNames();
/// @brief The protocol a table code belongs to, IR_PROTO_NONE for anything else.
IrProtocol getIrProtocol(uint32_t code);

// ---- the AC unit's believed state ------------------------------------------

enum AcIrMode
{
    AC_MODE_COOL = 0,
    AC_MODE_VENTILATOR = 1,
    AC_MODE_HUMIDIFIER = 2,
};

struct AcIrState
{
    bool power;
    uint8_t temp;
    AcIrMode mode;
    uint8_t fan; // 0 for low, 1 for high
    bool turbo;  // turbo mode -> min temp and max fan speed and cool mode
    bool led;
};

/// @brief What the AC unit is believed to be doing, kept in step with every AC code we
/// queue and every AC remote press we decode. Projector codes never touch it.
class AcIrStateController
{
public:
    AcIrState state;
    /// True once something has told us what the unit is actually doing -- a manual sync,
    /// or a remote press we decoded -- rather than the boot-time assumption of "off".
    bool known = false;

    AcIrStateController();
    /// @brief Turns the unit on or off by queueing AC_POWER when the belief differs.
    void setPower(bool state);
    /// @brief Walks the unit to a temperature by queueing every PLUS/MINUS step at once.
    /// Returns immediately; the pump transmits them IR_SEND_INTERVAL_MS apart. A unit that is
    /// off is turned on first and back off after, so the temperature lands where asked.
    void setTemp(uint8_t temp);
    void setMode(AcIrMode mode);
    /// @brief Advances the believed state as if `code` had been acted on by the unit.
    /// A code that is not the AC's is a no-op.
    void nextState(IrCodes code);
    /// @brief Overwrites the belief with what the unit's display shows. Sends nothing.
    void manualSync(bool power, uint8_t temp);
    void printState();
    String toJson();
};
extern AcIrStateController acIrState;

// ---- the transmitter -------------------------------------------------------

void enableIrDebug(bool enable);
bool getIrDebug();
/// @brief Queues any table code for transmission in its own protocol. An AC code also
/// advances the AC belief. False if the queue is full or the code is not in the table, in
/// which case nothing changes.
bool sendIRCode(uint32_t code);
void startIrServices();
void tickIrServices();

// ---- the receiver as a sensor ------------------------------------------

/// @brief What the receiver last decoded, as of one instant.
struct IrSensorStatus
{
    bool everReceived;      ///< Anything decoded since boot.
    uint32_t code;          ///< Raw code of the last frame.
    char name[24];          ///< getIrName() of it, or "<PROTOCOL>:0x<raw>" for a remote that is not ours.
    uint32_t lastReceiveMs; ///< millis() of the last frame; 0 if never.
};

/// @brief Snapshot of the last received frame, copied under lock.
IrSensorStatus irSensorStatus();
using IrReceivedHandler = void (*)(uint32_t code, const String& name);
void setIrReceivedHandler(IrReceivedHandler handler);
