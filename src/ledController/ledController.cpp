#include "ledController.h"
#if defined(BOARD_C6_V1)
#include <Adafruit_NeoPixel.h>
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
#endif
