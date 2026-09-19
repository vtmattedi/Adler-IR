#pragma once
#include <Arduino.h>
#include "NetworkSensor.h"

// The MQTT inbox. NightMareNetwork calls the message callback on the esp-mqtt
// task; on this single-core C3 that preempts loop() at any instruction. So the
// callback does no application work: it keeps only the topics a registered
// network sensor could want, copies them into a fixed queue and returns.
// Net_loop() drains the queue on the loop task and offers each message to the
// sensors, so every piece of application state is touched by one task.
//
// The queue drops rather than blocks when full: a callback that waits makes
// the MQTT client miss keep-alives, and the broker dropping the connection is
// worse than a missed reading.

#define NET_INBOX_DEPTH 8
#define NET_TOPIC_MAX 80
#define NET_PAYLOAD_MAX 224
#define NET_MAX_SENSORS 4
#define NET_DRAIN_PER_LOOP 4

/// @brief Registers a sensor to receive messages. Call before Net_begin().
void Net_registerSensor(NetworkSensor *sensor);
/// @brief Installs the MQTT callback and allocates the inbox.
void Net_begin();
/// @brief Drains the inbox. Call from loop().
void Net_loop();
/// @brief Messages the callback wanted but could not queue -- full, or too long. Should stay 0.
uint32_t Net_droppedMessages();
