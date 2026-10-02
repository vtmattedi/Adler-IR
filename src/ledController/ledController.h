#include <board.h>

#include <NightMareNetwork.h>

#if defined(BOARD_C6_V1)
/// The onboard RGB LED as a writable Resource, packed 0xRRGGBB. The module owns the declaration,
/// its name and its binding.
extern ManagedState<uint32_t> rgbLedColor;
/// Installs the write handler. The Resource binds itself in startNightMareESP().
void setupLed();
#endif

void writeLedColor(uint32_t color);