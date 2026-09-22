#pragma once

#include <Version.h>
#include <creds.h>
#include <cstring>
#include <cstdlib>

// Older Adler credentials stored one broker URI. New credentials may define
// the host and port macros directly.
#ifdef MQTT_URI
namespace AdlerBroker
{
inline const char *host()
{
    static char value[128] = {};
    if (!value[0])
    {
        const char *start = std::strstr(MQTT_URI, "://");
        start = start ? start + 3 : MQTT_URI;
        const char *end = std::strchr(start, ':');
        if (!end)
            end = start + std::strlen(start);
        size_t length = static_cast<size_t>(end - start);
        if (length >= sizeof(value))
            length = sizeof(value) - 1;
        std::memcpy(value, start, length);
        value[length] = '\0';
    }
    return value;
}

inline int port()
{
    const char *start = std::strstr(MQTT_URI, "://");
    start = start ? start + 3 : MQTT_URI;
    const char *separator = std::strchr(start, ':');
    return separator ? std::atoi(separator + 1) : 1883;
}
}

#ifndef LOCAL_MQTT_HOST
#define LOCAL_MQTT_HOST AdlerBroker::host()
#define LOCAL_MQTT_PORT AdlerBroker::port()
#endif
#endif

#if defined(MQTT_URI) && !defined(REMOTE_MQTT_URL)
#define REMOTE_MQTT_URL AdlerBroker::host()
#define REMOTE_MQTT_PORT AdlerBroker::port()
#endif

#ifndef MQTT_CREDS_H
#define MQTT_CREDS_H 1
#endif
#ifndef ROOT_CA
#define ROOT_CA ""
#endif

#define NM_ENABLE_SETTINGS 1
#define NM_ENABLE_RESOURCES 1
#define NM_ENABLE_NETWORK 1
#define NM_ENABLE_CONSOLE 1
#define NM_ENABLE_WIFI 1
#define NM_ENABLE_MQTT 1
#define NM_ENABLE_TELEMETRY 1
#define NM_ENABLE_SCHEDULER 1
#define NM_ENABLE_JOBS 1
#define NM_ENABLE_TIME_SYNC 1
#define NM_ENABLE_OTA 1
#define NM_ENABLE_HTTP 0
#define NM_ENABLE_WEBSOCKET 0
#define NM_ENABLE_LVGL 0
#define NM_CONSOLE_SERIAL 1
#define NM_FIRMWARE_VERSION VERSION

#define NM_LOG_LEVEL NM_LOG_LEVEL_TRACE 
