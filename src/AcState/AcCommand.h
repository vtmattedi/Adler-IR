#pragma once

#include <stdint.h>
#include <IrController/IrCodes.h>
#include "fifo.h"
#include <AcObservation/AcObservations.h>
constexpr size_t MAX_COMMAND_QUEUE_SIZE = 10;


enum class State : uint8_t
{
    WAITING_FOR_BEEP,
    CONFIRMED,
    TIMED_OUT,
};

enum class Source : uint8_t
{
    SENT,
    PHYSICAL_REMOTE,
};

struct AcCommand
{
    IrCodes code;
    Source source;
    State state;
    uint32_t sendTime = 0; // <0 means not sent yet
    int userId = 0;
};


// For now 1 ir -> next beep = confirmed.
// 2 ir code -> 1 beep  will confirm the first, tinvalidate the second.
// We may want to handle those cases in the future.
class AcCommandController
{

public:
    using IrResolvedHandler = void (*)(AcCommand command);
    
    // callback to when we resolve a command as a beep or a timeout. 
    void onIrResolved(IrResolvedHandler handler) { onResolved_ = handler;} 

    // An IR frame actually happened. SENT completes the transmit phase of the command queued
    // above. PHYSICAL_REMOTE starts confirmation directly because it is already on the air.
    // False means another command currently owns the confirmation slot or the code did not match.
    bool irDetected(IrCodes code, Source source, int userId = 0);
    
    // should be called when a beep is detected.
    void beepDetected();

    // Confirms the one frame currently waiting. Unexpected beeps return false and change nothing.
    void tick();

private:
    IrResolvedHandler onResolved_ = nullptr;
    FiFo<AcCommand, MAX_COMMAND_QUEUE_SIZE> waitingQueue_;
};


extern AcCommandController gAcCommandController;