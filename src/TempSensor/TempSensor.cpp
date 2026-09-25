#include "TempSensor.h"

#if BOARD_HAS_DS18B20

#include <NightMare.h>
#include <NightMare/NetResources.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/// Bound to the pin in setupTempSensor() rather than here, so nothing touches the GPIO during
/// static initialisation, before setup() has run.
static OneWire oneWire;
static DallasTemperature sensors(&oneWire);
/// Task-private state: only the task touches these, so they need no lock.
static DeviceAddress sensorAddress;
static bool sensorFound = false;
static uint8_t failures = 0;
static TaskHandle_t tempTaskHandle = nullptr;

/// What the getters hand out. Written only by the task, read from whichever task runs a command
/// or a report; the lock stops a reader catching half an update. A spinlock rather than a mutex
/// because every critical section is a few stores or one struct copy.
static portMUX_TYPE statusLock = portMUX_INITIALIZER_UNLOCKED;
static TempSensorStatus sensorStatus = {false, false, NAN, 0, ""};

/// @brief Enumerates the bus and caches the first sensor's address, so each read can address it
/// directly instead of repeating the ROM search.
static bool findSensor()
{
    static bool reportedMissing = false;
    sensors.begin();
    // setResolution() has to succeed too: the conversion wait in readSensor() is sized for
    // TEMP_RESOLUTION_BITS, and a sensor left at a higher resolution would be read mid-conversion.
    if (!sensors.getAddress(sensorAddress, 0) || !sensors.setResolution(sensorAddress, TEMP_RESOLUTION_BITS))
    {
        // Retried every interval; say it once rather than every 5 s.
        if (!reportedMissing)
            Serial.printf("TempSensor: no DS18B20 on GPIO%d, will keep looking\n", DS18B20_PIN);
        reportedMissing = true;
        return false;
    }
    reportedMissing = false;

    char addr[sizeof(sensorStatus.address)];
    for (int i = 0; i < 8; i++)
        snprintf(addr + i * 2, 3, "%02X", sensorAddress[i]);
    bool parasite = sensors.isParasitePowerMode();
    Serial.printf("TempSensor: DS18B20 %s on GPIO%d%s\n", addr, DS18B20_PIN, parasite ? " (parasite power)" : "");

    portENTER_CRITICAL(&statusLock);
    sensorStatus.connected = true;
    sensorStatus.parasite = parasite;
    memcpy(sensorStatus.address, addr, sizeof(addr));
    portEXIT_CRITICAL(&statusLock);
    return true;
}

/// @brief Counts a failed read; after TEMP_MAX_FAILURES in a row, drops the reading and goes
/// back to searching the bus.
static void recordFailure()
{
    if (++failures < TEMP_MAX_FAILURES)
        return;
    Serial.println("TempSensor: sensor stopped responding");
    failures = 0;
    sensorFound = false;

    // lastReadMs is kept: after a loss it says how long ago the last good reading was.
    portENTER_CRITICAL(&statusLock);
    sensorStatus.connected = false;
    sensorStatus.tempC = NAN;
    sensorStatus.address[0] = '\0';
    portEXIT_CRITICAL(&statusLock);
}

static void readSensor()
{
    // Asynchronous conversion (setWaitForConversion(false)): start it, then sleep through it. The
    // library's blocking mode instead polls the bus in a loop for the whole conversion, which at
    // this task's priority would keep loop() off the CPU for 750 ms on every read.
    if (!sensors.requestTemperaturesByAddress(sensorAddress))
    {
        recordFailure();
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(sensors.millisToWaitForConversion(TEMP_RESOLUTION_BITS)));

    float t = sensors.getTempC(sensorAddress);
    if (t == DEVICE_DISCONNECTED_C) // also what a scratchpad CRC failure returns
    {
        recordFailure();
        return;
    }
    failures = 0;
    uint32_t readMs = millis();

    portENTER_CRITICAL(&statusLock);
    sensorStatus.tempC = t;
    sensorStatus.lastReadMs = readMs;
    portEXIT_CRITICAL(&statusLock);

    temperatureSensor.setValue(t);
}

static void tempSensorTask(void *)
{
    for (;;)
    {
        uint32_t startMs = millis();
        // Sit out an OTA: every 1-Wire bit slot runs with interrupts masked, and an upload
        // stalling behind them fails.
        if (!SystemState.get(SystemFlag::OtaRunning))
        {
            if (!sensorFound)
                sensorFound = findSensor();
            if (sensorFound)
                readSensor();
        }
        uint32_t elapsed = millis() - startMs;
        uint32_t wait = (elapsed >= TEMP_READ_INTERVAL_MS) ? 1 : (TEMP_READ_INTERVAL_MS - elapsed);
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void setupTempSensor()
{
    if (tempTaskHandle)
        return; // already running

    oneWire.begin(DS18B20_PIN);
    sensors.setWaitForConversion(false);

    // Not pinned to a core: the task sleeps almost all the time, so wherever it wakes is fine.
    BaseType_t ok = xTaskCreate(tempSensorTask, "temp_sensor", TEMP_TASK_STACK, nullptr, TEMP_TASK_PRIORITY, &tempTaskHandle);
    if (ok != pdPASS)
    {
        tempTaskHandle = nullptr;
        Serial.println("TempSensor: failed to create sampling task");
    }
}

TempSensorStatus tempSensorStatus()
{
    portENTER_CRITICAL(&statusLock);
    TempSensorStatus copy = sensorStatus;
    portEXIT_CRITICAL(&statusLock);
    return copy;
}

float currentTemperature()
{
    return tempSensorStatus().tempC;
}

String Ds18ProbeJson(uint8_t pin)
{
    // Local bus and sensor: they go away when this returns, so probing the pin the real sensor is
    // on is allowed too. A probe overlapping the sampling task's conversion may spoil that one read.
    OneWire bus(pin);
    DallasTemperature probe(&bus);
    probe.begin();

    uint8_t deviceCount = probe.getDeviceCount();
    String json = "{";
    json += "\"pin\": " + String(pin) + ",";
    json += "\"devices\": " + String(deviceCount);

    if (deviceCount == 0)
    {
        // Idle level separates the two ways this fails: a floating bus reads low, while a
        // pulled-up bus with nothing on it reads high.
        json += ",\"idleLevel\": " + String(digitalRead(pin));
        json += ",\"hint\": \"nothing answered; check the 4k7 pull-up to 3V3, sensor power, and that this is the data pin\"";
    }
    else
    {
        DeviceAddress address;
        String addressText = "";
        if (probe.getAddress(address, 0))
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                if (address[i] < 0x10)
                    addressText += "0";
                addressText += String(address[i], HEX);
            }
        }
        probe.setResolution(TEMP_RESOLUTION_BITS);
        probe.requestTemperatures(); // blocking: setWaitForConversion defaults to true
        float temperature = probe.getTempCByIndex(0);
        json += ",\"address\": \"" + addressText + "\"";
        json += ",\"parasitePower\": " + String(probe.isParasitePowerMode() ? "true" : "false");
        json += ",\"tempC\": " + String(temperature, 2);
        json += ",\"valid\": " + String(temperature == DEVICE_DISCONNECTED_C ? "false" : "true");
    }
    json += "}";
    return json;
}

#endif // BOARD_HAS_DS18B20
