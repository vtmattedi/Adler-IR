#include <Arduino.h>
#include <NightMare.h>
#include <Version.h>
#include <board.h>
#include <Board/BoardInfo.h>
#include <TempSensor/TempSensor.h>
#include <IrController/IrController.h>
#include <AcController/AcController.h>
#include <NightMare/NetResources.h>
#include <LittleFS.h>
// Adler: the air-conditioner controller.
//   sensors     DS18B20 (local), the IR receiver (local), the door (another device, over MQTT)
//   actuator    the IR transmitter

// ---- the resolver -------------------------------------------------------------------

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    if (gAc.handles(message.command))
    {
        NightMareResults result = gAc.command(message);
        syncAcResources();
        return result;
    }

    NightMareResults res;
    res.result = false;
    res.response = "not implemented";
#if defined(BOARD_C6_V1)
    if (message.command == "RGB")
    {
        unsigned long color = 0;
        if (message.argc > 0)
        {
            color = strtoul(message.args[0].c_str(), nullptr, 16);
            rgbLedColor.setValue(color);
            res.result = true;
            res.response = "RGB LED color set to: " + String(color, HEX);
        }
        else
        {
            color = rgbLedColor.getValue();
            res.result = true;
            res.response = "RGB LED current color: " + String(color, HEX);
        }
    }
    else
#endif
    {
        Serial.printf("Command received: %s\n", message.command.c_str());
    }
    return res;
}

// ---- lifecycle -------------------------------------------------------------------------

void setup()
{
    introNightMareESP();
    printBoardInfo();

    setCommandResolver(localHandleNightMareCommand);
    gAc.begin(temperatureSensor, doorSensor);
    if (!bindResources())
        Serial.println("Adler: one or more NightMare resources failed to bind");
    syncAcResources();
#if BOARD_HAS_DS18B20
    setupTempSensor();
#endif
    startIrServices();
    startNightMareESP();
#if defined(BOARD_C6_V1)
    pinMode(PIN_BUTTON, INPUT_PULLUP);

#endif
}

void loop()
{
    gAc.loop();
    syncAcResources();
    pumpIrServices();
    tickNightMareESP();
#if defined(BOARD_C6_V1)
    static bool val = digitalRead(PIN_BUTTON);
    if (val != digitalRead(PIN_BUTTON))
    {
        if (val)
        {
            LOG("Button", "pressed");
            gAc.setPower(!acIrState.state.power);
            syncAcResources();
        }
        else
            LOG("Button", "released");

        val = !val;
    };
#endif
}
