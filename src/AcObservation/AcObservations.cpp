#include "AcObservations.h"
#include <AcController/AcController.h>

RemoteSensor<bool> acInputFan("ac:input:fan");
RemoteSensor<bool> acInputCompressor("ac:input:compressor");
RemoteEvent<std::string> acInputBeep("ac:input:beep");
constexpr const char *tag = "\x1b[36mOBS\x1b[0m";
static void (*onBeepHandler)(void) = nullptr;
namespace
{
    /// Runs on the network task. AcController serializes it with IR callbacks from other tasks.
    void onBeep(RemoteEvent<std::string> &, const std::string &)
    {
        LOG(tag, "beep detected");
        if (onBeepHandler)
            onBeepHandler();
    }

} // namespace

void setOnBeepHandler(void (*handler)(void))
{
    onBeepHandler = handler;
}

void onFanChange(bool fan)
{
    gAcObservedState.fan = fan;
    gAcObservedState.lastFanChange = millis();
}

void onCompressorChange(bool compressor)
{
    gAcObservedState.compressor = compressor;
    gAcObservedState.lastCompressorChange = millis();
}
AcObservedState gAcObservedState;

void installAcObservations()
{
    acInputBeep.onEvent = onBeep;
}
bool AcObservedState::isValid() const
{
    return acInputFan.isStale() == false &&
           acInputCompressor.isStale() == false &&
           acInputFan.available() == true &&
           acInputCompressor.available() == true;
}