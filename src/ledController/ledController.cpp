#include "ledController.h"
#if defined(BOARD_C6_V1)
#include <Adafruit_NeoPixel.h>
#include <atomic>
static bool ledControllerInitialized = false;
Adafruit_NeoPixel strip(1, PIN_RGB_DATA, NEO_GRB + NEO_KHZ800);

struct rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

rgb fromInt(uint32_t color) {
    rgb result;
    result.r = (color >> 16) & 0xFF;
    result.g = (color >> 8) & 0xFF;
    result.b = color & 0xFF;
    return result;
}

void writeLedColor(uint32_t color)
{
    // Implement the logic to write the color to the LED hardware.
    // This is a placeholder implementation; replace it with actual hardware control code.
    // For example, if using an RGB LED strip, you might use a library like FastLED or Adafruit NeoPixel.
    // Here, we just print the color value for demonstration purposes.
    Serial.printf("Setting LED color to: 0x%06X\n", color);
    if (!ledControllerInitialized)
    {
        // Initialize the LED controller hardware here if needed.
        ledControllerInitialized = true;

        pinMode(PIN_RGB_DATA, OUTPUT);     // Example: Set built-in LED pin as output
        pinMode(PIN_RGB_POWER, OUTPUT);    // Example: Set LED pin as output (replace LED_PIN with actual pin number)
        digitalWrite(PIN_RGB_POWER, HIGH); // Example: Turn on the LED power (if applicable)
        strip.begin();                     // Initialize the NeoPixel strip
        strip.show();                      // Initialize all pixels to 'off'
    }

    rgb colorRGB = fromInt(color);
    strip.setPixelColor(0, strip.Color(colorRGB.r, colorRGB.g, colorRGB.b));
    strip.show();
    // Implement the logic to set the LED color.
    // write color to WS2812;
}

ManagedState<uint32_t> rgbLedColor("rgb");

static bool onRgbWrite(ManagedState<uint32_t> &, const uint32_t &requested)
{
    writeLedColor(requested);
    return true;
}

void setupLed()
{
    rgbLedColor.onWrite = onRgbWrite;
}

// ---- network status ---------------------------------------------------------------
// Dim on purpose: the LED sits next to the unit in a dark room.
static constexpr uint32_t kColorNone = 0x300000;   // red:    nothing is up
static constexpr uint32_t kColorRadio = 0x302000;  // yellow: Wi-Fi radio only
static constexpr uint32_t kColorIp = 0x000030;     // blue:   Wi-Fi IP, no transport
static constexpr uint32_t kColorMqtt = 0x003030;   // cyan:   MQTT connected
static constexpr uint32_t kColorEspNow = 0x003000; // green:  ESP-NOW connected

static std::atomic<uint8_t> layersUp{0};              // bit per NetworkLayer
static std::atomic<uint32_t> statusColor{kColorNone}; // what the status should show
static uint32_t shownColor = 0xFFFFFFFF;              // never a real colour: forces the first write
static uint32_t shownSinceMs = 0;                     // when shownColor was written
static bool ledBlanked = false;                       // shownColor has been switched off again
static constexpr uint32_t kConnectedLedMs = 10000;    // how long a connected colour stays lit

static uint32_t colorForLayers(uint8_t up)
{
    using NightMare::NetworkLayer;
    auto has = [up](NetworkLayer layer) { return (up >> static_cast<uint8_t>(layer)) & 1; };
    if (has(NetworkLayer::ESP_NOW))
        return kColorEspNow;
    if (has(NetworkLayer::MQTT))
        return kColorMqtt;
    if (has(NetworkLayer::WIFI_IP))
        return kColorIp;
    if (has(NetworkLayer::WIFI_RADIO))
        return kColorRadio;
    return kColorNone;
}

/// Runs on the Wi-Fi, MQTT and ESP-NOW tasks: records the state, touches no hardware.
static void onNetworkStatus(const NightMare::NetworkStatusEvent &event)
{
    const uint8_t bit = 1u << static_cast<uint8_t>(event.layer);
    const uint8_t up = event.up ? (layersUp.fetch_or(bit) | bit) : (layersUp.fetch_and(~bit) & ~bit);
    statusColor.store(colorForLayers(up));
}

void setupStatusLed()
{
    NightMare::OnNetworkStatus(onNetworkStatus);
}

void tickStatusLed()
{
    const uint32_t color = statusColor.load();
    const uint32_t now = millis();
    if (color != shownColor)
    {
        shownColor = color;
        shownSinceMs = now;
        ledBlanked = false;
        writeLedColor(color);
        return;
    }
    // Connected is the settled state, so the LED only confirms it for a while. Everything short
    // of it stays lit: it is telling someone that the device is not there yet.
    const bool connected = color == kColorEspNow || color == kColorMqtt;
    if (connected && !ledBlanked && now - shownSinceMs >= kConnectedLedMs)
    {
        ledBlanked = true;
        writeLedColor(0);
    }
}
#endif
