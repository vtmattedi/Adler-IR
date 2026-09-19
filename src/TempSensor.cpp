#include <TempSensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

static OneWire bus;
static DallasTemperature sensors(&bus);
static DeviceAddress address;
static TempSensorStatus status = {false, false, NAN, 0, ""};
static uint32_t nextReadMs = 0;
static uint32_t conversionStartedMs = 0;
static bool converting = false;
static uint8_t failures = 0;

void setupTempSensor() {
    bus.begin(DS18B20_PIN);
    sensors.setWaitForConversion(false);
}

void rescanTempSensor() {
    status.connected = false;
    status.tempC = NAN;
    status.address[0] = '\0';
    converting = false;
    failures = 0;
    nextReadMs = 0;
}

static void failedRead() {
    if (++failures >= TEMP_MAX_FAILURES) rescanTempSensor();
}

void tickTempSensor() {
    const uint32_t nowMs = millis();
    if (converting) {
        if (static_cast<uint32_t>(nowMs - conversionStartedMs) <
            sensors.millisToWaitForConversion(TEMP_RESOLUTION_BITS)) return;
        converting = false;
        float reading = sensors.getTempC(address);
        if (reading == DEVICE_DISCONNECTED_C) failedRead();
        else {
            failures = 0;
            status.tempC = reading;
            status.lastReadMs = nowMs;
        }
        nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
        return;
    }
    if (static_cast<int32_t>(nowMs - nextReadMs) < 0) return;
    if (!status.connected) {
        sensors.begin();
        if (!sensors.getAddress(address, 0) ||
            !sensors.setResolution(address, TEMP_RESOLUTION_BITS)) {
            nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
            return;
        }
        status.connected = true;
        status.parasite = sensors.isParasitePowerMode();
        for (int i = 0; i < 8; ++i)
            snprintf(status.address + i * 2, 3, "%02X", address[i]);
        Serial.printf("[temp] DS18B20 %s on GPIO%d\n", status.address, DS18B20_PIN);
    }
    if (!sensors.requestTemperaturesByAddress(address)) {
        failedRead();
        nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
        return;
    }
    conversionStartedMs = nowMs;
    converting = true;
}

float currentTemperature() { return status.tempC; }
TempSensorStatus tempSensorStatus() { return status; }

String Ds18ProbeJson(uint8_t pin) {
    OneWire probeBus(pin);
    DallasTemperature probe(&probeBus);
    probe.begin();
    StaticJsonDocument<256> result;
    result["pin"] = pin;
    result["devices"] = probe.getDeviceCount();
    if (probe.getDeviceCount()) {
        probe.setResolution(TEMP_RESOLUTION_BITS);
        probe.requestTemperatures();
        float reading = probe.getTempCByIndex(0);
        if (reading != DEVICE_DISCONNECTED_C) result["tempC"] = reading;
    } else result["idleLevel"] = digitalRead(pin);
    String json;
    serializeJson(result, json);
    return json;
}
