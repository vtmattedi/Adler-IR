#pragma once

#include <stdint.h>
#include <IrController/IrCodes.h>
#include "fifo.h"
#include <AcState/AcCommand.h>
#include <IrController/IrController.h>
// this class is responsible for sending IR commands and confirming them with beeps. It manages a queue of commands and handles the timing of sending and confirming them.

struct ControlledCommand
{
    IrCodes code;
    int userId;
    int state = 0;
    bool active = false;
    uint32_t timestamp;// ts of the last state change
};

class AcSender
{
public:
    void begin();
    void handleIrResolved(AcCommand command);
    // sends a command and waits for confirmation. Returns the command id if queued, -1 if the queue is full or not started yet.
    // user can check it status by doing getCommand(id) and checking the state.
    // state is = 0: waiting, 1: sending, 2: waiting_for_beep, 3: confirmed, 4: timed_out
    int sendCommandWithAnswer(IrCodes code);
    ControlledCommand getCommand(int id) const;
    constexpr static size_t MAX_COMMANDS_QUEUE_SIZE = 10;
    ControlledCommand commands[MAX_COMMANDS_QUEUE_SIZE];
    // ticks the state machine, should be called in the main loop.
    void tick();
};

extern AcSender gAcSender;

bool launchAsNonblocking(IrCodes code);