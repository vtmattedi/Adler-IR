#include <AdlerSettings.h>
#include <LittleFS.h>

AdlerSettings settings;

bool AdlerSettings::begin() {
    _mounted = LittleFS.begin(true);
    if (!_mounted) return false;
    File file = LittleFS.open("/configs.json", "r");
    if (!file) return true;
    auto error = deserializeJson(_values, file);
    file.close();
    if (error || !_values.is<JsonObject>()) {
        _values.clear();
        _values.to<JsonObject>();
        return false;
    }
    return true;
}

String AdlerSettings::get(const char* key, const String& fallback) const {
    JsonVariantConst value = _values[key];
    return value.is<const char*>() ? value.as<String>() : fallback;
}

bool AdlerSettings::set(const char* key, const String& value) {
    if (!key || !*key || !_mounted) return false;
    if (get(key) == value) return true;
    _values[key] = value;
    return !_values.overflowed() && save();
}

bool AdlerSettings::save() const {
    if (!_mounted || _values.overflowed()) return false;
    File file = LittleFS.open("/configs.json", "w");
    if (!file) return false;
    size_t written = serializeJson(_values, file);
    file.close();
    return written > 0;
}
