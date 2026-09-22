#pragma once
#include <Arduino.h>

/* ---------------------------------------------------------------------
 * Adler board revision registry.
 *
 * Every hardware revision this firmware has ever run on keeps its pin map
 * here, forever. A unit in the field is the only thing that knows how it is
 * wired, and it cannot tell us -- so this file has to.
 *
 * RULES
 *   1. Append only. Once a unit has shipped with a revision, that block is
 *      the record of how that unit is wired: never edit it and never delete
 *      it. Wiring changed? Add a new revision below.
 *   2. Exactly one revision is selected, with -D BOARD_<CHIP>_V<N> from the
 *      env in platformio.ini -- never from a source file. The guard below
 *      fails the build on none or more than one.
 *   3. Pin numbers are macros on purpose. Unselected revisions cost nothing
 *      because the preprocessor drops them before the compiler runs. Keep
 *      runtime data (the table in boardinfo.h, strings, anything that lands
 *      in .rodata) derived from the selected revision's macros only -- a
 *      per-revision table would put every revision in flash and lose this.
 *   4. Record what is physically wired, including pins this firmware does
 *      not use. Losing that record is the whole failure this file prevents.
 *   5. Never reuse a revision name for different wiring. Names are permanent.
 *
 * Revision names are BOARD_-prefixed rather than bare ESP_C3_V1 to keep them
 * clearly apart from ESP32_C3, which platformio.ini also defines and which
 * means something unrelated (NightMareNetwork's HWCDC console switch).
 * ------------------------------------------------------------------- */

#if (defined(BOARD_C3_V1) + defined(BOARD_C6_V1) + defined(BOARD_ESP32_V1)) != 1
#error "Select exactly one board revision, e.g. -D BOARD_C3_V1 in platformio.ini. See include/board.h."
#endif

/* =====================================================================
 * BOARD_C3_V1 -- ESP32-C3 SuperMini (nologo_esp32c3_super_mini).
 * The unit currently deployed.
 * ===================================================================== */
#if defined(BOARD_C3_V1)

#define BOARD_NAME "ESP32-C3 SuperMini rev1"

#define PIN_IR_RECEIVE 7   // TSOP receiver output
#define PIN_IR_LED 8       // IR transmit LED, driven by LEDC at 38 kHz
#define PIN_ONBOARD_LED 9  // status LED
#define ONBOARD_LED_ON HIGH 
#define ONBOARD_LED_OFF LOW
#define PIN_ONE_WIRE 10 // DS18B20 data line
#define BOARD_HAS_IR_RECEIVER 1
#define BOARD_HAS_DS18B20 1

// GPIO 11-17 are bonded to the SPI flash and 18/19 carry the USB console.
#define BOARD_GPIO_MAX 21
#define BOARD_GPIO_IS_RESERVED(g) ((g) >= 11 && (g) <= 19)

/* GPIO8 and GPIO9 are both C3 strapping pins -- together they select the boot
 * mode -- and GPIO9 is the SuperMini's BOOT button pad. GPIO9 held low through
 * reset gives serial download mode instead of booting, and GPIO8 low at the
 * same time is an invalid combination. First thing to check if the unit ever
 * stops starting.
 *
 * GPIO8 is also the pin the SuperMini's own blue LED sits on, so the IR LED
 * shares it with that LED and its series resistor. */

/* =====================================================================
 * BOARD_C6_V1 -- M5Stack NanoC6, built against esp32-c6-devkitm-1.
 * Uses only the connections built into the board. There is no IR receiver or
 * DS18B20, but its onboard IR transmitter is fully supported.
 * ===================================================================== */
#elif defined(BOARD_C6_V1)

#define BOARD_NAME "M5Stack NanoC6 rev1"

#define PIN_IR_LED 3      // IR transmit LED
#define PIN_ONBOARD_LED 7 // blue user LED
#define ONBOARD_LED_ON HIGH
#define ONBOARD_LED_OFF LOW
#define PIN_BUTTON 9    // user / BOOT button, active-low, internal pull-up
#define PIN_RGB_DATA 20 // on-board WS2812 data line
#define PIN_RGB_POWER 19 // WS2812 power enable, drive HIGH to power it
#define PIN_IO_1 1       // broken out, unused
#define PIN_IO_2 2       // broken out, unused
#define BOARD_HAS_IR_RECEIVER 0
#define BOARD_HAS_DS18B20 0

// GPIO 12/13 carry the USB console, 24-30 are the SPI flash.
#define BOARD_GPIO_MAX 30
#define BOARD_GPIO_IS_RESERVED(g) (((g) >= 12 && (g) <= 13) || ((g) >= 24 && (g) <= 30))

// No PIN_IR_RECEIVE and no PIN_ONE_WIRE: neither is fitted on this board.

/* =====================================================================
 * BOARD_ESP32_V1 -- original classic ESP32, esp32doit-devkit-v1.
 * The first Adler. Kept buildable: its peripheral set is complete.
 * ===================================================================== */
#elif defined(BOARD_ESP32_V1)

#define BOARD_NAME "ESP32 DevKit v1 rev1"

#define PIN_IR_RECEIVE 23  // TSOP receiver output
#define PIN_IR_LED 3       // IR transmit LED
#define PIN_ONBOARD_LED 2  // on-module LED
#define ONBOARD_LED_ON HIGH
#define ONBOARD_LED_OFF LOW
#define PIN_ONE_WIRE 16 // DS18B20 data line
#define BOARD_HAS_IR_RECEIVER 1
#define BOARD_HAS_DS18B20 1

/* Pins this firmware no longer drives, recorded so the wiring is not lost.
 * Note PIN_PZEM_RX collides with PIN_IR_RECEIVE on 23: that collision was
 * present in the original firmware, it is not a transcription error. */
#define PIN_LDR 33      // LDR, analog
#define PIN_PZEM_RX 23  // PZEM-004T, to the ESP32 TX pin
#define PIN_PZEM_TX 22  // PZEM-004T, to the ESP32 RX pin
#define PIN_ZMPT101B 34 // mains voltage sense, analog, input-only pin
#define PIN_ACS712 35   // current sense, analog, input-only pin

// GPIO 6-11 are bonded to the SPI flash. 34-39 exist but are input-only.
#define BOARD_GPIO_MAX 39
#define BOARD_GPIO_IS_RESERVED(g) ((g) >= 6 && (g) <= 11)

#endif // revision selection

/// @brief Whether a GPIO exists on the selected board and is safe for a debug
/// command to poke. Flash and USB-console pins are excluded: driving either
/// takes the unit down rather than returning an error.
inline bool isUsableGpio(uint8_t gpio)
{
    return gpio <= BOARD_GPIO_MAX && !BOARD_GPIO_IS_RESERVED(gpio);
}
