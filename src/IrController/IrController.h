#pragma once
#include <Arduino.h>
#include <NightMare.h>
#include <board.h>
#include "IrCodes.h"
// IrController is transport. Two tasks, started by startIrServices():
//   tx   sends the codes queued by sendIRCode(), one every IR_SEND_INTERVAL_MS
//   rx   (boards with a demodulator) decodes whatever is heard and publishes it through the
//        managed `ir:recv` sensor, whichever remote it came from
// Both hand a frame to AcCommand if, and only if, it is a code for the AC: tx once the frame has
// left the LED, rx when the AC's own remote was heard. What the unit did about it is AcCommand's.

// Deep enough for the longest sequence a transaction plans (a full 18->30 walk and back).
#define IR_ASYNC_QUEUE_SIZE 48
// Gap between transmitted codes. The unit needs time to act on one before the next.
#define IR_SEND_INTERVAL_MS 200
// A code the receiver hears this soon after we transmitted the same code is our own
// LED bouncing off the wall, not the remote.
#define IR_SELF_ECHO_WINDOW_MS 400
#define IR_TASK_STACK_SIZE 4096
#define IR_TASK_PRIORITY 1

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

struct IrSendPayload
{
    IrCodes code;
    uint32_t userId; // for the report on tester/ir/confirmation
};
/// @brief Queues any table code for transmission in its own protocol. Safe from any task.
/// @param code The code to send.
/// @return id if the code was queued, 0 if the queue is full, not started yet, or the code is not in the table.
uint32_t sendIRCode(IrCodes code);

/// @brief Starts the transmit task and, on boards with a receiver, the receive task. Call from setup().
void startIrServices();
