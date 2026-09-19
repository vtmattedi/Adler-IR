#include "Net.h"
#include <NightMareNetwork.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

namespace
{
    struct NetItem
    {
        char topic[NET_TOPIC_MAX];
        char payload[NET_PAYLOAD_MAX];
    };

    NetworkSensor *sensors[NET_MAX_SENSORS] = {nullptr};
    uint8_t sensorCount = 0;
    QueueHandle_t inbox = nullptr;
    // Incremented on the MQTT task, read on the loop task.
    std::atomic<uint32_t> dropped{0};

    /// Whether the topic's first segment names a device one of the sensors is bound to.
    /// Runs on the MQTT task; compares against String members that only loop() writes, and a
    /// torn read here costs at worst one misrouted message that the loop side then rejects.
    bool wanted(const char *topic)
    {
        const char *slash = strchr(topic, '/');
        if (!slash || slash == topic)
            return false;
        size_t deviceLen = slash - topic;
        const char *channel = slash + 1;
        bool isSensors = strncmp(channel, "sensors", 7) == 0 && (channel[7] == '\0' || channel[7] == '/');
        bool isStatus = strcmp(channel, "status") == 0;
        if (!isSensors && !isStatus)
            return false;
        for (uint8_t i = 0; i < sensorCount; i++)
        {
            const String &d = sensors[i]->device();
            if (d.length() == deviceLen && strncmp(d.c_str(), topic, deviceLen) == 0)
                return true;
        }
        return false;
    }

    /// MQTT task. Copy into the inbox; nothing else.
    void onMqttMessage(String topic, String payload)
    {
        if (!inbox || !wanted(topic.c_str()))
            return;
        // An empty payload is a tombstone -- a retained message being cleared -- not a reading.
        // A retained empty status still matters (device deleted), so only readings are skipped.
        if (!payload.length() && topic.indexOf("/status") < 0)
            return;
        if (topic.length() >= NET_TOPIC_MAX || payload.length() >= NET_PAYLOAD_MAX)
        {
            dropped++;
            return;
        }
        NetItem item;
        strncpy(item.topic, topic.c_str(), NET_TOPIC_MAX - 1);
        item.topic[NET_TOPIC_MAX - 1] = '\0';
        strncpy(item.payload, payload.c_str(), NET_PAYLOAD_MAX - 1);
        item.payload[NET_PAYLOAD_MAX - 1] = '\0';
        if (xQueueSend(inbox, &item, 0) != pdTRUE)
            dropped++;
    }
}

void Net_registerSensor(NetworkSensor *sensor)
{
    if (sensor && sensorCount < NET_MAX_SENSORS)
        sensors[sensorCount++] = sensor;
}

void Net_begin()
{
    inbox = xQueueCreate(NET_INBOX_DEPTH, sizeof(NetItem));
    if (!inbox)
        Serial.println("[net] inbox allocation failed; network sensors will never update");
    // Every topic on the broker, prefix intact: the sensors we want live under other devices.
    MQTT_onMessage(onMqttMessage, false);
}

void Net_loop()
{
    if (!inbox)
        return;
    NetItem item;
    for (uint8_t n = 0; n < NET_DRAIN_PER_LOOP && xQueueReceive(inbox, &item, 0) == pdTRUE; n++)
    {
        String topic(item.topic);
        String payload(item.payload);
        for (uint8_t i = 0; i < sensorCount; i++)
            sensors[i]->offer(topic, payload);
    }
}

uint32_t Net_droppedMessages()
{
    return dropped.load();
}
