#include <Arduino.h>
#include <NightMare.h>
#include <Version.h>
#include <board.h>
#include <Board/BoardInfo.h>
#include <TempSensor/TempSensor.h>
#include <IrController/IrController.h>
#include <AcController/AcController.h>
#include <AcObservation/AcObservations.h>
#include <AcState/AcSender.h>
#if defined(BOARD_C6_V1)
#include <ledController/ledController.h>
#endif
// Adler: the air-conditioner controller for the room.
//   observes   Watson's fan, compressor and beeps (NightMare Remote Resources), the IR receiver,
//              the door (another device), the local DS18B20
//   decides    AcController (being rebuilt)
//   acts       AcCommand over the IR transmitter, with one pending beep confirmation
// main.cpp is wiring only.

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
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
            res.response = "RGB LED color set to: " + NightMare::Text::hex(color);
        }
        else
        {
            color = rgbLedColor.getValue();
            res.result = true;
            res.response = "RGB LED current color: " + NightMare::Text::hex(color);
        }
    }
    else
#endif
        if (message.command == "AC")
        {
            bool a = launchAsNonblocking(IrCodes::AC_POWER);
            res.result = true;
            res.response = a ? "AC POWER command sent" : "AC POWER command not sent (busy)";
        }
        if (message.command == "IR")
        {
            if (message.argc > 0)
            {
                IrCodes code = static_cast<IrCodes>(NightMare::Text::toLong(message.args[0]));

                IrCodes irCode = getIrCodeByName(message.args[0]);
                if (irCode != IrCodes::NO_CODE)
                {
                    sendIRCode(irCode);
                }
                else if (code != 0)
                {
                    sendIRCode(static_cast<IrCodes>(code));
                }
                else
                {
                    res.result = false;
                    res.response = "Unknown IR code name: " + message.args[0];
                }
            }
            else
            {
                res.result = false;
                res.response = "No IR code name provided";
            }
        }
        else
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

    installAcObservations();
    setupTempSensor();
    startIrServices();
#if defined(BOARD_C6_V1)
    setupLed();
    setupStatusLed();
#endif
    startNightMareESP();

#if defined(BOARD_C6_V1)
    pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif
    gAcSender.begin();
}

void loop()
{
    tickNightMareESP();
#if defined(BOARD_C6_V1)
    tickStatusLed();
#endif
    gAcCommandController.tick();
    gAcSender.tick();
#if defined(BOARD_C6_V1)
    static bool val = digitalRead(PIN_BUTTON);
    if (val != digitalRead(PIN_BUTTON))
    {
        if (val)
        {
            LOG("Button", "pressed");
            gAcSender.sendCommandWithAnswer(IrCodes::AC_POWER);
        }
        else
            LOG("Button", "released");

        val = !val;
    };
#endif
}
