#include <Arduino.h>
#include <NightMare.h>
#include <Version.h>
#include <board.h>
#include <Board/BoardInfo.h>
#include <TempSensor/TempSensor.h>
#include <IrController/IrController.h>
#include <AcController/AcController.h>
#include <LittleFS.h>
#include <acGuesser.h>
#if defined(BOARD_C6_V1)
#include <ledController/ledController.h>
#endif
// Adler: the air-conditioner controller.
//   sensors     DS18B20 (local), the IR receiver (local), the door (another device, over MQTT)
//   actuator    the IR transmitter

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    if (gAc.handles(message.command))
    {
        NightMareResults result = gAc.command(message);
        gAc.publishResources();
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
        if (message.command == "IR")
    {
        if (message.argc > 0)
        {
            uint32_t code = getIrCode(message.args[0]);
            if (code != 0)
            {
                if (sendIRCode(code))
                {
                    res.result = true;
                    res.response = "IR code sent: " + String(message.args[0]);
                }
                else
                {
                    res.result = false;
                    res.response = "Failed to send IR code: " + String(message.args[0]);
                }
            }
            else
            {
                res.result = false;
                res.response = "Unknown IR code name: " + String(message.args[0]);
            }
        }

        else
        {
            res.result = false;
            res.response = "No IR code name provided";
        }
    }
    else if (message.command == "AT")
    {
        Serial.printf("AC Guess: \n");
        Serial.printf("\t-Last IR Code: %s\n", getIrName(acGuess.lastIrCode).c_str());
        Serial.printf("\t\t-Last IR Sent: %lu\n", acGuess.lastIrSent);
        Serial.printf("\t-Last Beep: %lu\n", acGuess.lastbeep);
        Serial.printf("\t-Fan Guess: %s\n", acGuess.fanguess ? "true" : "false");
        Serial.printf("\t\t-Last Fan Change: %lu\n", acGuess.lastfanchange);
        Serial.printf("\t-Compressor Guess: %s\n", acGuess.compressorguess ? "true" : "false");
        Serial.printf("\t\t-Last Compressor Change: %lu\n", acGuess.lastcompressorchange);
        res.result = true;
        res.response = "AC Guess state printed to Serial";
    }
    else
    {
        Serial.printf("Command received: %s\n", message.command.c_str());
    }
    return res;
}

// ---- lifecycle -------------------------------------------------------------------------

void handleTestMessage(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    Serial.printf("MQTT message received: topic=%s, payload=%.*s, retained=%d\n", topic, (int)length, payload, retained);
}

void setup()
{

    introNightMareESP();
    printBoardInfo();
    NightMare::Subscribe("test/#");
    NightMare::OnMessage(handleTestMessage);
    setCommandResolver(localHandleNightMareCommand);
    // Each module declares and keeps its own Resources and Configs; startNightMareESP() binds them.
    gAc.begin();
    gAc.publishResources();
    setupTempSensor();
    startIrServices();
#if defined(BOARD_C6_V1)
    setupLed();
#endif
    startNightMareESP();
    startAcGuess();
#if defined(BOARD_C6_V1)
    pinMode(PIN_BUTTON, INPUT_PULLUP);

#endif
    // int results[WIFI_POWER_21dBm] = {0};
    // for (int i = 0; i < WIFI_POWER_21dBm; ++i)
    // {
    //     bool is_power = powerStr((wifi_power_t)i) != "unknown";
    //     if (!is_power)
    //         {
    //             results[i] = -1;
    //             continue;
    //         }
    //     wifi_connect((wifi_power_t)i);
    //     results[i] = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    // }
    // for (int i = 0; i < WIFI_POWER_21dBm; ++i)
    // {
    //     if (results[i] == -1)
    //         continue;
    //     Serial.printf("WiFi power %s dBm %s\n", powerStr((wifi_power_t)i), OK_LOG(results[i] == 1));
    // }
}

void loop()
{

    gAc.loop();
    gAc.publishResources();
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
            gAc.publishResources();
        }
        else
            LOG("Button", "released");

        val = !val;
    };
#endif
}
