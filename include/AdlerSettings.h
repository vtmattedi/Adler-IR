#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Adler's application settings remain in the existing LittleFS file so deployed
// units retain their AC settings, Wi-Fi credentials, and door binding.
class AdlerSettings {
public:
    bool begin();
    String get(const char* key, const String& fallback = "") const;
    bool set(const char* key, const String& value);
    bool save() const;
private:
    DynamicJsonDocument _values{4096};
    bool _mounted = false;
};

extern AdlerSettings settings;
