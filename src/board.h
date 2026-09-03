#pragma once
#include <Arduino.h>
/* M5Stack NanoC6 Board Definitions */

// ---- M5NanoC6 pin map ------------------------------------------------
#define ONBOARD_LED_ON HIGH
#define ONBOARD_LED_OFF LOW
#define PIN_ONBOARD_BLUE_LED   7   // blue user LED (active-high)
#define PIN_RGB_DATA   20  // on-board WS2812 RGB LED data line
#define PIN_RGB_POWER  19  // RGB LED power enable (drive HIGH to power it)
#define PIN_BUTTON     9   // user / BOOT button (active-low, has pull-up)
#define PIN_IR_LED     3   // IR LED TX pin
#define IO_PIN_1       1  
#define IO_PIN_2       2  


// QoL Funcitons

static uint8_t _current_rgb_data[3] = {0};
static void rgbLedWrite(uint8_t r, uint8_t g, uint8_t b)
{
    if (_current_rgb_data[0] != r || _current_rgb_data[1] != g || _current_rgb_data[2] != b)
    {
        _current_rgb_data[0] = r;
        _current_rgb_data[1] = g;
        _current_rgb_data[2] = b;
        digitalWrite(PIN_RGB_POWER, HIGH); // Ensure power is on
        // Use the built-in WS2812 driver for core 3.x
        rgbLedWrite(PIN_RGB_DATA, r >> 3, g >> 3, b >> 3);  // dimmed to ~12%

    }
}
static void rgbLedWrite(uint32_t rgb)
{
    rgbLedWrite((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

uint32_t getRgbCurrentColor()
{
    return (_current_rgb_data[0] << 16) | (_current_rgb_data[1] << 8) | _current_rgb_data[2];
}

void initM5NanoC6Board()
{
    pinMode(PIN_ONBOARD_BLUE_LED, OUTPUT);
    pinMode(PIN_RGB_POWER, OUTPUT);
    digitalWrite(PIN_RGB_POWER, HIGH); // Ensure power is on
    rgbLedWrite(0x000000);              // turn off RGB LED
}

void rgbTask(void *pvParameters)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

