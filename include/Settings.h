#pragma once

#include <cstdint>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h is missing - copy include/secrets.example.h to secrets.h and fill it in!"
#endif

#ifndef DB_LOG_PATH
#define DB_LOG_PATH "/Log/"
#endif

#ifndef DB_STAT_PATH
#define DB_STAT_PATH "/stat/"
#endif

#ifndef DB_REMOTE_PATH
#define DB_REMOTE_PATH "/remote/" // older secrets.h: default path of the app purchase requests
#endif

#ifndef REMOTE_DEVICE_ID
#define REMOTE_DEVICE_ID "machine1" // <dev> in /remote/<dev>/... - the app must use the same id
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "" // older secrets.h: OTA disabled
#endif

// Hardware independent settings. The pinout is in the files under boards/.
namespace settings
{
    constexpr const char *kFirmwareName = "BrewPass";
    constexpr const char *kFirmwareVersion = "2.2.1";
    constexpr const char *kHostname = "brewpass"; // mDNS: brewpass.local (OTA)
    // NVS namespace - kept from the old project name (rfidValidator), so the saved price/mode survives the rename
    constexpr const char *kNvsNamespace = "rfidval";

    // Time: Central European Time with automatic DST switching (replaces the old manual DST calculation)
    constexpr const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    constexpr const char *kNtpServer1 = "europe.pool.ntp.org";
    constexpr const char *kNtpServer2 = "pool.ntp.org";

    // OTA (ArduinoOTA / espota): <kHostname>.local, on this port
    constexpr uint16_t kOtaPort = 3232;

    // Watchdog: if the main loop or the network task does not check in for this long, the device restarts
    constexpr uint32_t kWatchdogTimeoutS = 45;

    // WiFi
    constexpr uint32_t kWifiConnectTimeoutMs = 8000;
    constexpr uint32_t kWifiRetryIntervalMs = 5000;
    constexpr uint32_t kWifiRssiRefreshMs = 5000;

    // Firebase (time limits of the synchronous operations in the background task)
    constexpr uint32_t kFirebaseSyncTimeoutS = 8;
    constexpr uint32_t kFirebaseHandshakeTimeoutS = 8;
#if defined(BOARD_TARGET)
    // plain ESP32 without PSRAM: smaller network task stack and request queue (the free stack is in the health log)
    constexpr uint32_t kCloudTaskStackBytes = 12288;
    constexpr uint8_t kCloudRequestQueueLen = 8;
#else
    constexpr uint32_t kCloudTaskStackBytes = 16384;
    constexpr uint8_t kCloudRequestQueueLen = 16;
#endif
    constexpr uint8_t kCloudTaskPriority = 1;
    constexpr uint8_t kCloudTaskCore = 0;
    constexpr uint8_t kCloudResponseQueueLen = 8;
    constexpr uint8_t kLogMaxAttempts = 3;
    // Recovery from a stuck database connection (E22/E23 on every request while WiFi is up):
    // after this many failed requests in a row the request connection is reopened and the remote request
    // stream (the third TLS connection) is paused to free its memory ...
    constexpr uint8_t kCloudFailStreakRecover = 3;
    constexpr uint32_t kRemoteStreamPauseMs = 5UL * 60UL * 1000UL;
    // ... and if no request has succeeded for this long, the device restarts (only in an idle state)
    constexpr uint32_t kCloudStuckRestartMs = 5UL * 60UL * 1000UL;

    // Coffee machine interface (NRI G-13 emulation) - the original timing: 2 pulses, 50 ms LOW / 50 ms HIGH
    constexpr uint8_t kCoinPulseCount = 2;
    constexpr uint32_t kCoinPulseLowMs = 50;
    constexpr uint32_t kCoinPulseHighMs = 50;
    constexpr uint32_t kInhibitDebounceMs = 20;

    // RFID reader health check
    constexpr uint32_t kReaderHealthCheckMs = 5000;
    constexpr uint32_t kReaderRecoverRetryMs = 3000;

    // Remote purchase from the app ("virtual card tap", /remote/<dev>/req stream).
    // The request stream is a third TLS connection (~40 KB heap): it is only started if at least this much
    // heap is free, so the card purchases never run out of memory because of it.
    constexpr bool kRemoteVendEnabled = true;
    // Target (plain ESP32, no PSRAM): the config and the app requests are POLLED over the single request
    // connection instead of two extra always-open TLS streams (~40 KB heap each) - the three connections
    // left too little memory, and the requests failed (E23) until a reset. The T-Embed has PSRAM: streams.
#if defined(BOARD_TARGET)
    constexpr bool kUseStreams = false;
#else
    constexpr bool kUseStreams = true;
#endif
    constexpr uint32_t kConfigPollMs = 30000;
    constexpr uint32_t kRemotePollMs = 3000;
    constexpr uint32_t kRemoteStreamMinFreeHeap = 70000;
    constexpr uint32_t kRemoteStreamHeapRetryMs = 60000;

    // Preventive restart (the original firmware also needed a periodic restart): once a day at this local
    // hour, only in an idle state and only after at least kDailyRestartMinUptimeS of uptime
    constexpr bool kDailyRestartEnabled = true;
    constexpr int kDailyRestartHour = 3;
    constexpr uint32_t kDailyRestartMinUptimeS = 2UL * 60UL * 60UL;

    // Diagnostics
    constexpr uint32_t kHeapReportIntervalMs = 60000;
} // namespace settings
