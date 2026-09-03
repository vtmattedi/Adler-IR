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

IrStateController::IrStateController()
{
    state.power = false;
    state.temp = MIN_AC_TEMP;
    state.mode = IrModes::MODE_COOL;
    state.fan = 0;
    state.led = false;
    state.turbo = false;
}

void IrStateController::nextState(IrCodes code)
{
    if (code == POWER)
    {
        state.power = !state.power;
        state.mode = IrModes::MODE_COOL; // reset to cool mode when toggling power
        return;
    }
    if (!state.power)
    {
        // If the AC is off, only the POWER command should have an effect
        return;
    }
    // if we are on turbo mode, +,- should not have an effect on the temp
    if (code == PLUS && !state.turbo)
    {
        if (state.temp < MAX_AC_TEMP)
            state.temp++;
    }
    else if (code == MINUS && !state.turbo)
    {
        if (state.temp > MIN_AC_TEMP)
            state.temp--;
    }
    else if (code == LED)
    {
        state.led = !state.led;
    }
    else if (code == TURBO)
    {
        state.turbo = !state.turbo;
        state.mode = IrModes::MODE_COOL; // when turbo is turned on, the mode should be set to cool
    }
    else if (code == MODE)
    {
        if (state.turbo)
        {
            state.turbo = false;
        }
        else
        {
            state.mode = static_cast<IrModes>((state.mode + 1) % 3);
        }
    }
}

void IrStateController::setPower(bool powerState)
{
    if (state.power != powerState)
    {
        state.power = powerState;
        sendIRCode(POWER); // toggle power state
    }
}

void IrStateController::setTemp(uint8_t temp)
{
    if (temp < MIN_AC_TEMP || temp > MAX_AC_TEMP)
        return; // Invalid temperature, ignore the command
    bool wasOn = state.power;
    if (!wasOn)
    {
        sendIRCode(POWER); // turn on the AC if it was off
        delay(200);
    }
    while (this->state.temp != temp)
    {
        if (this->state.temp < temp)
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

void IrStateController::setMode(IrModes mode)
{
    if (this->state.mode != mode && state.power)
    {
        if (mode == IrModes::MODE_COOL)
        {
            sendIRCode(MODE); // set to cool mode
        }
        else if (mode == IrModes::MODE_VENTILATOR)
        {
            if (this->state.mode == IrModes::MODE_COOL)
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
            if (this->state.mode == IrModes::MODE_COOL)
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

void IrStateController::printState()
{
    Serial.println("Current IR State:");
    Serial.println("Power: " + String(state.power ? "ON" : "OFF"));
    Serial.println("Temperature: " + String(state.temp) + "°C");
    Serial.println("Mode: " + String(state.mode == MODE_COOL ? "COOL" : state.mode == MODE_VENTILATOR ? "VENTILATOR"
                                                                                                      : "HUMIDIFIER"));
    Serial.println("Fan: " + String(state.fan == 0 ? "LOW" : "HIGH"));
    Serial.println("Turbo: " + String(state.turbo ? "ON" : "OFF"));
    Serial.println("LED: " + String(state.led ? "ON" : "OFF"));
}

void forceAcToTemp(uint8_t temp, bool turnOn = false)
{
    if (turnOn)
    {
        irController.setPower(true);
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
        irController.setPower(false);
    }
}

String IrStateController::toJson()
{
    DynamicJsonDocument doc(256);
    doc["power"] = state.power;
    doc["temp"] = state.temp;
    doc["mode"] = state.mode;
    doc["fan"] = state.fan;
    doc["turbo"] = state.turbo;
    doc["led"] = state.led;
    String json;
    serializeJson(doc, json);
    return json;
}

IrStateController irController;
bool irDebugEnabled = false;

void enableIrDebug(bool enable)
{
    irDebugEnabled = enable;
    Config.setFlag("ir_debug", enable);
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
    irController.nextState(static_cast<IrCodes>(code));
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
    if (true)
    {
        Serial.println("Sending IR code: " + getIrName(codeToSend));
    }

    lastIrSendTime = millis(); // Update the last send time
    IrSender.sendPulseDistanceWidth(38, 9000, 4550, 600, 1700, 600, 550, codeToSend, 24, PROTOCOL_IS_LSB_FIRST, 100, 1);

    irController.nextState(static_cast<IrCodes>(codeToSend));
}

void IRReceiveHandler(void *pvParameters)
{
    IrReceiver.begin(IR_RECEIVE_PIN);
    unsigned long lastReceiveTime = 0;
    uint32_t lastReceivedCode = 0;
    for (;;)
    {
        if (IrReceiver.decode())
        {   
            if (IrReceiver.decodedIRData.protocol == PULSE_DISTANCE && !((IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) != 0))
            {
                uint32_t receivedCode = IrReceiver.decodedIRData.decodedRawData;
                if (receivedCode == lastReceivedCode && millis() - lastReceiveTime < 200)
                    ;
                else
                {
                    if (irDebugEnabled)
                    {
                        Serial.printf("[%lu][%d] Raw data: %lX\n", millis() - lastReceiveTime, receivedCode == lastReceivedCode, receivedCode);
                        Serial.println("Received IR code: " + getIrName(receivedCode));
                        Send_to_MQTT("ir/received", getIrName(receivedCode));
                    }
                    // Serial.printf("%s\n", getIrName(receivedCode).c_str());
                    // Serial.printf("%s\n", irController.toJson().c_str());
                    irController.nextState(static_cast<IrCodes>(receivedCode));
                    // Serial.println(irController.toJson());
                    // IrReceiver.printIRResultShort(&Serial);
                    lastReceivedCode = receivedCode;
                    lastReceiveTime = millis();
                }
            }
            IrReceiver.resume(); // Prepare for the next IR code
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay to prevent task watchdog timer from triggering
    }
}

void startIrServices()
{
    pinMode(IR_SEND_PIN, OUTPUT);
    pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);
    enableIrDebug(Config.getFlag("ir_debug"));
    IrSender.begin(IR_SEND_PIN);
    // runs the handleIrAsync function every 5 ms.
    Timers.create("IR Async Handler", 5, handleIrAsync, true);
    BaseType_t res = xTaskCreatePinnedToCore(IRReceiveHandler, "IR Receive Handler", IR_RECEIVE_TASK_STACK_SIZE, NULL, IR_RECEIVE_TASK_PRIORITY, NULL, 0);
    Serial.printf("%s IR Receive Handler task created\n", OK_LOG(res == pdPASS));
}