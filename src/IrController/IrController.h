#pragma once
#include <Arduino.h>
#include <NightMare.h>
#include <board.h>
#include "IrCodes.h"


// The IR side of Adler always provides transmission:
//   an actuator -- codes queued by sendIRCode() and transmitted by a pump. Codes are
//   generic: the table knows each one's protocol. The AC's own codes also advance
//   the state the AC unit is believed to be in (AcIrStateController) as they are queued;
#if BOARD_HAS_IR_RECEIVER
// Boards with a demodulator also provide a sensor -- whatever it hears is decoded and handed
//   to the network through the managed `ir:recv` sensor, whichever remote it came from.
#endif

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
#define IR_RECEIVE_TASK_STACK_SIZE 4096
#define IR_RECEIVE_TASK_PRIORITY 1

// Both are constants rather than macros on purpose. IRSend.hpp does
//     #if defined(IR_SEND_PIN)
//     #define sendPin IR_SEND_PIN
// so a macro named IR_SEND_PIN would replace IrSender's runtime sendPin member
// and quietly ignore the pin handed to IrSender.begin(). IR_RECEIVE_PIN carries
// no such meaning for IrReceiver, but is kept symmetrical with it.
constexpr uint8_t IR_SEND_PIN = PIN_IR_LED;
#if BOARD_HAS_IR_RECEIVER
constexpr uint8_t IR_RECEIVE_PIN = PIN_IR_RECEIVE;
#endif

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
    bool setPower(bool state);
    /// @brief Walks the unit to a temperature by queueing every PLUS/MINUS step at once.
    /// Returns immediately; the pump transmits them IR_SEND_INTERVAL_MS apart. A unit that is
    /// off is turned on first and back off after, so the temperature lands where asked.
    bool setTemp(uint8_t temp);
    bool setMode(AcIrMode mode);
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
/// @brief Who asked for a transmission. Every send is tracked by the AC guesser; only the AC
/// controller's own sends are reported on `tester/ir/confirmation`.
enum IrSource : uint8_t
{
    IR_SRC_DIRECT = 0,     ///< console `IR`, the `ir:send` action: someone driving the LED by hand
    IR_SRC_AC_CONTROLLER,  ///< the AcController policy and its AC state helper
};

/// @brief Queues any table code for transmission in its own protocol. An AC code also
/// advances the AC belief. False if the queue is full or the code is not in the table, in
/// which case nothing changes.
bool sendIRCode(uint32_t code, IrSource source = IR_SRC_DIRECT);
/// @brief Pin assignment, debug flag, pending queue depth and the AC belief as JSON.
String IrInfoJson();
void startIrServices();
void pumpIrServices();

// ---- the receiver as a sensor ------------------------------------------

#if BOARD_HAS_IR_RECEIVER
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
#endif
