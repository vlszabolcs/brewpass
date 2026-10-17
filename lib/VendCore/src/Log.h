#pragma once

// Simple leveled serial logging. Hardware independent: native tests print via printf.
//   LOGE - error, LOGW - warning, LOGI - info, LOGD - verbose (only with VENDCORE_LOG_DEBUG)

#if defined(ARDUINO)
#include <Arduino.h>
#define VLOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define VLOG_MS() ((unsigned long)millis())
#else
#include <cstdio>
#define VLOG_PRINTF(...) std::printf(__VA_ARGS__)
#define VLOG_MS() 0UL
#endif

#define VLOG(level, tag, fmt, ...) VLOG_PRINTF("[%9lu][%c][%s] " fmt "\n", VLOG_MS(), level, tag, ##__VA_ARGS__)

#define LOGE(tag, fmt, ...) VLOG('E', tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) VLOG('W', tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) VLOG('I', tag, fmt, ##__VA_ARGS__)

#if defined(VENDCORE_LOG_DEBUG)
#define LOGD(tag, fmt, ...) VLOG('D', tag, fmt, ##__VA_ARGS__)
#else
#define LOGD(tag, fmt, ...) \
    do                      \
    {                       \
    } while (0)
#endif
