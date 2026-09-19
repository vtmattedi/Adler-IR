#include "NetworkSensor.h"
#include <NightMareNetwork.h>

NetworkSensor::NetworkSensor(const char *name) : _name(name) {}

void NetworkSensor::loadBinding()
{
    String n(_name);
    _device = Config.get(n + "_device", "");
    _key = Config.get(n + "_key", "");
    _invert = Config.get(n + "_invert", "0") == "1";
    // Write the keys back so a fresh unit's config shows them, empty, ready to be set.
    Config.set(n + "_device", _device);
    Config.set(n + "_key", _key);
    Config.set(n + "_invert", _invert ? "1" : "0");
    if (isBound())
        Serial.printf("[net] %s bound to %s/sensors/%s%s\n", _name, _device.c_str(), _key.c_str(), _invert ? " (inverted)" : "");
    else
        Serial.printf("[net] %s not bound; set %s_device and %s_key\n", _name, _name, _name);
}

void NetworkSensor::bind(const String &device, const String &key, bool invert)
{
    String n(_name);
    _device = device;
    _key = key;
    _invert = invert;
    _hasValue = false;
    _raw = "";
    _lastSeenMs = 0;
    _deviceOnline = true;
    Config.set(n + "_device", _device);
    Config.set(n + "_key", _key);
    Config.set(n + "_invert", _invert ? "1" : "0");
    Config.save();
}

void NetworkSensor::unbind()
{
    bind("", "", false);
}

void NetworkSensor::ingest(const String &keyPath, const String &value)
{
    if (keyPath != _key)
        return;
    _raw = value;
    _raw.trim();
    _hasValue = _raw.length() > 0;
    _lastSeenMs = millis();
}

void NetworkSensor::ingestObject(const String &prefix, JsonObjectConst obj, uint8_t depth)
{
    if (depth > 3)
        return;
    for (JsonPairConst kv : obj)
    {
        String childKey = prefix.length() ? prefix + "/" + kv.key().c_str() : String(kv.key().c_str());
        if (kv.value().is<JsonObjectConst>())
        {
            ingestObject(childKey, kv.value().as<JsonObjectConst>(), depth + 1);
        }
        else
        {
            String v;
            if (kv.value().isNull())
                v = "";
            else if (kv.value().is<bool>())
                v = kv.value().as<bool>() ? "true" : "false";
            else
                v = kv.value().as<String>();
            ingest(childKey, v);
        }
    }
}

bool NetworkSensor::offer(const String &topic, const String &payload)
{
    if (!isBound())
        return false;
    int slash = topic.indexOf('/');
    if (slash <= 0 || topic.substring(0, slash) != _device)
        return false;

    String rest = topic.substring(slash + 1);
    int second = rest.indexOf('/');
    String channel = second < 0 ? rest : rest.substring(0, second);

    if (channel.equalsIgnoreCase("status"))
    {
        // Retained "offline" from the last-will means nothing it published since is trustworthy.
        String s = payload;
        s.trim();
        s.toLowerCase();
        _deviceOnline = s.length() && s != "offline";
        return true;
    }
    if (!channel.equalsIgnoreCase("sensors"))
        return false;

    String keyPath = second < 0 ? String("") : rest.substring(second + 1);
    String trimmed = payload;
    trimmed.trim();
    if (trimmed.startsWith("{"))
    {
        // One object carries several sensors; each field becomes "<keyPath>/<field>".
        DynamicJsonDocument doc(768);
        if (deserializeJson(doc, trimmed) == DeserializationError::Ok && doc.is<JsonObjectConst>())
            ingestObject(keyPath, doc.as<JsonObjectConst>(), 0);
        return true;
    }
    if (keyPath.length())
        ingest(keyPath, trimmed);
    return true;
}

bool NetworkSensor::asBool(bool &out) const
{
    if (!_hasValue)
        return false;
    String v = _raw;
    v.toLowerCase();
    bool one;
    if (v == "1" || v == "true")
        one = true;
    else if (v == "0" || v == "false")
        one = false;
    else
        return false; // not a boolean reading; a wrong "closed" is worse than an honest unknown
    out = _invert ? !one : one;
    return true;
}

float NetworkSensor::asFloat() const
{
    if (!_hasValue)
        return NAN;
    char *end = nullptr;
    float f = strtof(_raw.c_str(), &end);
    return (end && *end == '\0') ? f : NAN;
}

NetworkSensorStatus NetworkSensor::status() const
{
    NetworkSensorStatus s;
    s.bound = isBound();
    s.hasValue = _hasValue;
    s.deviceOnline = _deviceOnline;
    s.raw = _raw;
    s.lastSeenMs = _lastSeenMs;
    s.fresh = _hasValue && _deviceOnline && (millis() - _lastSeenMs) < NETWORK_SENSOR_STALE_MS;
    return s;
}

void NetworkSensor::info(JsonObject into) const
{
    NetworkSensorStatus s = status();
    JsonObject o = into.createNestedObject(_name);
    o["source"] = "network";
    o["device"] = _device;
    o["key"] = _key;
    o["invert"] = _invert;
    o["bound"] = s.bound;
    o["fresh"] = s.fresh;
    o["deviceOnline"] = s.deviceOnline;
    if (s.hasValue)
    {
        o["value"] = s.raw;
        o["age_ms"] = millis() - s.lastSeenMs;
    }
    else
    {
        o["value"] = nullptr;
    }
}
