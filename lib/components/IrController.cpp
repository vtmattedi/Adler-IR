#include "IrController.h"
#include <IRremote.hpp>
String getIrName(uint32_t IrCode)
{
    switch (IrCode)
    {
    case POWER:
        return "POWER";
    case PLUS:
        return "PLUS";
    case MINUS:
        return "MINUS";
    case COUNT_DOWN:
        return "COUNT_DOWN";
    case LED:
        return "LED";
    case TURBO:
        return "TURBO";
    case MODE:
        return "MODE";
    case VENTILATOR:
        return "VENTILATOR";
    case SLEEP1:
        return "SLEEP1";
    case SLEEP2:
        return "SLEEP2";
    case SLEEP3:
        return "SLEEP3";
    default:
        return "UNKNOWN";
    }
}

IrState::IrState()
{
    power = false;
    temp = MIN_AC_TEMP;
    mode = IrModes::MODE_COOL;
    fan = 0;
    led = false;
    turbo = false;
}

void IrState::nextState(IrCodes code)
{
    if (code == POWER)
    {
        power = !power;
        mode = IrModes::MODE_COOL; // reset to cool mode when toggling power
        return;
    }
    if (!power)
    {
        // If the AC is off, only the POWER command should have an effect
        return;
    }
    // if we are on turbo mode, +,- should not have an effect on the temp
    if (code == PLUS && !turbo)
    {
        if (temp < MAX_AC_TEMP)
            temp++;
    }
    else if (code == MINUS && !turbo)
    {
        if (temp > MIN_AC_TEMP)
            temp--;
    }
    else if (code == LED)
    {
        led = !led;
    }
    else if (code == TURBO)
    {
        turbo = !turbo;
        mode = IrModes::MODE_COOL; // when turbo is turned on, the mode should be set to cool
    }
    else if (code == MODE)
    {
        if (turbo)
        {
            turbo = false;
        }
        else
        {
            mode = static_cast<IrModes>((mode + 1) % 3);
        }
    }
}

void IrState::setPower(bool state)
{
    if (power != state)
    {
        power = state;
        sendIRCode(POWER); // toggle power state
    }
}

void IrState::setTemp(uint8_t temp)
{
    if (temp < MIN_AC_TEMP || temp > MAX_AC_TEMP)
        return; // Invalid temperature, ignore the command
    bool wasOn = power;
    if (!wasOn)
    {
        sendIRCode(POWER); // turn on the AC if it was off
        delay(200);
    }
    while (this->temp != temp)
    {
        if (this->temp < temp)
        {
            sendIRCode(PLUS);
        }
        else
        {
            sendIRCode(MINUS);
        }
        delay(200); // Small delay to ensure the AC unit has time to process the command
    }
    if (!wasOn)
    {
        sendIRCode(POWER); // turn off the AC if it was originally off
    }
}

void IrState::setMode(IrModes mode)
{
    if (this->mode != mode && power)
    {
        if (mode == IrModes::MODE_COOL)
        {
            sendIRCode(MODE); // set to cool mode
        }
        else if (mode == IrModes::MODE_VENTILATOR)
        {
            if (this->mode == IrModes::MODE_COOL)
            {
                sendIRCode(MODE); // cool -> ventilator
            }
            else
            {
                sendIRCode(MODE); // humidifier -> ventilator
                sendIRCode(MODE);
            }
        }
        else if (mode == IrModes::MODE_HUMIDIFIER)
        {
            if (this->mode == IrModes::MODE_COOL)
            {
                sendIRCode(MODE); // cool -> humidifier
                sendIRCode(MODE);
            }
            else
            {
                sendIRCode(MODE); // ventilator -> humidifier
                sendIRCode(MODE);
            }
        }
    }
}

void forceAcToTemp(uint8_t temp, bool turnOn = false)
{
    if (turnOn)
    {
        currentIrState.setPower(true);
        delay(150); // Small delay to ensure the AC unit has time to process the command
    }
    // ensures it is at Max temp.
    for (uint8_t t = MIN_AC_TEMP; t <= MAX_AC_TEMP; t++)
    {
        sendIRCode(PLUS);
        delay(150); // Small delay to ensure the AC unit has time to process the command
    }
    // now bring it down to the desired temp.
    for (uint8_t t = MAX_AC_TEMP; t > temp; t--)
    {
        sendIRCode(MINUS);
        delay(150); // Small delay to ensure the AC unit has time to process the command
    }
    if (turnOn)
    {
        currentIrState.setPower(false);
    }
}

IrState currentIrState;
bool irDebugEnabled = false;

void enableIrDebug(bool enable)
{
    irDebugEnabled = enable;
}

bool getIrDebug()
{
    return irDebugEnabled;
}

uint32_t _irQueue[IR_ASYNC_QUEUE_SIZE] = {0};
uint8_t _irQueueWrite = 0;
uint8_t _irQueueReadIndex = 0;

bool sendIRCode(uint32_t code)
{
    noInterrupts();
    bool isQueueFull = _irQueue[_irQueueWrite] != 0;
    if (isQueueFull)
    {
        interrupts();
        if (irDebugEnabled)
        {
            Serial.println("IR queue is full. Cannot send code: " + getIrName(code));
        }
        return false; // Queue is full, cannot send code
    }
    _irQueue[_irQueueWrite] = code;
    _irQueueWrite = (_irQueueWrite + 1) % IR_ASYNC_QUEUE_SIZE;
    interrupts();

    // Here you would add the actual code to send the IR signal using your IR transmitter hardware.
    return true; // Return true if the code was sent successfully, false otherwise.
}

void handleIrAsync()
{
    static unsigned long lastIrSendTime = 0;
    uint32_t codeToSend = 0;
    if (millis() - lastIrSendTime < IR_SEND_INTERVAL_MS)
    {
        return; // Not enough time has passed since the last IR code was sent
    }
    noInterrupts();
    if (_irQueue[_irQueueReadIndex] != 0)
    {
        codeToSend = _irQueue[_irQueueReadIndex];
        _irQueue[_irQueueReadIndex] = 0;                                   // Mark this slot as empty after sending
        _irQueueReadIndex = (_irQueueReadIndex + 1) % IR_ASYNC_QUEUE_SIZE; // Move to the next code in the queue
    }
    interrupts();

    if (codeToSend == 0)
    {
        return;
    }

    // Send the IR code using your IR transmitter hardware here.
    if (irDebugEnabled)
    {
        Serial.println("Sending IR code: " + getIrName(codeToSend));
    }

    lastIrSendTime = millis(); // Update the last send time
    IrSender.sendPulseDistanceWidth(38, 9000, 4550, 600, 1700, 600, 550, codeToSend, 24, PROTOCOL_IS_LSB_FIRST, 10, 1);
    currentIrState.nextState(static_cast<IrCodes>(codeToSend));
}

void startIrServices()
{
    pinMode(IR_PIN, OUTPUT);
    IrSender.begin(IR_PIN);
    // runs the handleIrAsync function every 5 ms.
    Timers.create("IR Async Handler", 5, handleIrAsync, true);
}