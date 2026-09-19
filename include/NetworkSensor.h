#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// A sensor whose readings live on another device. It has the same shape as a
// local sensor -- a value, an age, "unknown" when there is none -- so a
// controller does not know or care where its input comes from. Bound at
// runtime by device name and sensor key, persisted as
//   <name>_device, <name>_key, <name>_invert
// which are the Dashboard's key names, on purpose: the same sensor is described
// the same way everywhere on the network.
//
// Fed from the loop task by Net_loop() with every "<Other>/sensors" and
// "<Other>/status" message; it keeps only its own key.

/// No reading for this long and the value is stale -- the same window ServerVariable uses.
#define NETWORK_SENSOR_STALE_MS (10UL * 60UL * 1000UL)

struct NetworkSensorStatus
{
    bool bound;         ///< A device and key are configured.
    bool hasValue;      ///< A reading has arrived since boot.
    bool fresh;         ///< hasValue, within the stale window, and the device is not known offline.
    bool deviceOnline;  ///< Last retained status seen for the device; true until told otherwise.
    String raw;         ///< The reading as published.
    uint32_t lastSeenMs;
};

class NetworkSensor
{
public:
    /// @param name Config prefix and label: "door" -> door_device, door_key, door_invert.
    explicit NetworkSensor(const char *name);

    /// @brief Loads the binding from Config. Call after Config.begin().
    void loadBinding();
    /// @brief Points this sensor at a device and key, and persists it. Empty device unbinds.
    void bind(const String &device, const String &key, bool invert);
    void unbind();
    bool isBound() const { return _device.length() && _key.length(); }

    /// @brief Offer a network message. Loop task only. Returns true if it was for this sensor.
    bool offer(const String &topic, const String &payload);

    /// @brief The reading as a boolean. Only "1"/"true"/"0"/"false" count; anything else is not
    /// a reading and returns false, leaving `out` untouched. Inversion from the binding applied.
    bool asBool(bool &out) const;
    /// @brief The reading as a number, NAN when there is none or it does not parse.
    float asFloat() const;
    NetworkSensorStatus status() const;

    /// @brief This binding's entry in the descriptor.
    void info(JsonObject into) const;

    const String &device() const { return _device; }
    const String &key() const { return _key; }
    bool inverted() const { return _invert; }
    const char *name() const { return _name; }

private:
    const char *_name;
    String _device, _key;
    bool _invert = false;
    String _raw;
    bool _hasValue = false;
    bool _deviceOnline = true;
    uint32_t _lastSeenMs = 0;

    void ingest(const String &keyPath, const String &value);
    void ingestObject(const String &prefix, JsonObjectConst obj, uint8_t depth);
};
