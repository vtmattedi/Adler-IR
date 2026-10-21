#include <board.h>

#include <NightMareNetwork.h>

#if defined(BOARD_C6_V1)
/// The onboard RGB LED as a writable Resource, packed 0xRRGGBB. The module owns the declaration,
/// its name and its binding.
extern ManagedState<uint32_t> rgbLedColor;
/// Installs the write handler. The Resource binds itself in startNightMareESP().
void setupLed();
/// Shows the network state on the LED: red nothing, yellow radio, blue Wi-Fi IP, cyan MQTT, green
/// ESP-NOW; a connected colour switches off after 10 s. Install before startNightMareESP() so no layer is missed, then call tickStatusLed()
/// from loop(): the handler runs on the network tasks and only records the colour.
void setupStatusLed();
void tickStatusLed();
#endif

void writeLedColor(uint32_t color);