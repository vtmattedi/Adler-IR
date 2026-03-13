#pragma once
#include <Arduino.h>
#include <NightMareNetwork.h>


#define MIN_AC_TEMP 18
#define MAX_AC_TEMP 30
#define IR_ASYNC_QUEUE_SIZE 10
#define IR_SEND_INTERVAL_MS 100
#define IR_PIN 19
/// @brief The IR commands of MY AC unit.
enum IrCodes
{
    POWER = 0x10001,
    PLUS = 0x40004,
    MINUS = 0x20002,
    COUNT_DOWN = 0x400040,
    LED = 0x800080,
    TURBO = 0x200020,
    MODE = 0x100010,
    VENTILATOR = 0x80008,
    SLEEP1 = 0x10100,
    SLEEP2 = 0x20200,
    SLEEP3 = 0x40400
};

enum IrModes
{
    MODE_COOL = 0,
    MODE_VENTILATOR = 1,
    MODE_HUMIDIFIER = 2,
};

/// @brief Gets the IR command from the code value.
/// @param IrCode The code.
/// @return A String with the name of the code.
String getIrName(uint32_t IrCode);

class IrState
{
public:
    bool power;
    uint8_t temp;
    IrModes mode; 
    uint8_t fan;// 0 for low, 1 for high
    bool turbo; // turbo mode -> min temp and max fan speed and cool mode
    bool led; 
    /// @brief Default constructor for IrState.
    /// Initializes all IR controller state members to their default values.
    IrState();
    void setPower(bool state);
    void setTemp(uint8_t temp);
    void setMode(IrModes mode);
    void nextState(IrCodes code);
};
extern IrState currentIrState;

void enableIrDebug(bool enable);
bool getIrDebug();
bool sendIRCode(uint32_t code);
void startIrServices();