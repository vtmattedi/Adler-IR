#include "AcCommand.h"
#include <Core/Logs.h>
#include <Core/Config.h>
#include <Util/Lock.h>

static const char *tag = "\x1b[38mACCMD\x1b[0m";
static bool begin = false;

static SemaphoreHandle_t mtx = nullptr;

Config<int> acBeepTimeout("ac:config:beep_timeout", 5000, false); // milliseconds

bool initAcCommand()
{
    if (mtx == nullptr)
        mtx = xSemaphoreCreateMutex();
    if (acBeepTimeout.value() < 1000 || acBeepTimeout.value() > 15000)
        acBeepTimeout.set(5000);
    acBeepTimeout.onWrite = [](Config<int> &, const int &requested)
    {
        if (requested < 1000 || requested > 15000)
            return false;
        return true;
    };
    setOnBeepHandler([]()
    {
        gAcCommandController.beepDetected();
    });
    return mtx != nullptr;
}

bool AcCommandController::irDetected(IrCodes code, Source source, int userId)
{

    if (!mtx)
    {
        LOG_ERROR(tag, "AC command controller mutex not initialized");
        return false;
    }
    Lock lock(mtx);
    LOG(tag, "IR detected: code=0x%08lX, source=%s, userId=%d", (unsigned long)code,
        source == Source::SENT ? "SENT" : "PHYSICAL_REMOTE", userId);
    if (gAcObservedState.isValid() == false)
    {
        LOG(tag, "Ignoring IR command because AC state is not valid");
        return false;
    }
    return waitingQueue_.push({.code = code,
                               .source = source,
                               .state = State::WAITING_FOR_BEEP,
                               .sendTime = millis(),
                               .userId = userId});
}

void AcCommandController::beepDetected()
{
    if (!mtx)
    {
        LOG_ERROR(tag, "AC command controller mutex not initialized");
        return;
    }
    AcCommand command;
    {
        Lock lock(mtx);
        if (!waitingQueue_.peek(command))
        {
            LOG(tag, "No pending command to confirm on beep");
            return;
        }
        waitingQueue_.pop(command);
    }
    command.state = State::CONFIRMED;
    LOG(tag, "AC command confirmed: code=0x%08lX, source=%s, userId=%d", (unsigned long)command.code,
        command.source == Source::SENT ? "SENT" : "PHYSICAL_REMOTE", command.userId);
    if (onResolved_)
        onResolved_(command);
}

/// @brief Drains the waiting queue and marks all commands as timed out if the timeout has expired.
/// @note should be called periodically, e.g., in the main loop or a timer interrupt.
void AcCommandController::tick()
{
    if (!begin)
    {
        bool res = initAcCommand();
        res ? LOG(tag, "AC command controller initialized") : LOG_ERROR(tag, "AC command controller failed to initialize");
        begin = res;
        if (!begin)
            return;
    }

    for (;;)
    {
        AcCommand command;
        {
            Lock lock(mtx);
            // The queue is in send order, so the first unexpired command ends the scan.
            if (!waitingQueue_.peek(command) ||
                millis() - command.sendTime <= (uint32_t)acBeepTimeout.value())
                return;
            waitingQueue_.pop(command);
        }
        command.state = State::TIMED_OUT;
        LOG(tag, "AC command timed out: code=0x%08lX, source=%s, userId=%d", (unsigned long)command.code,
            command.source == Source::SENT ? "SENT" : "PHYSICAL_REMOTE", command.userId);
        if (onResolved_)
            onResolved_(command);
    }
}

AcCommandController gAcCommandController;