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
#define REMOTE_DEVICE_ID "brewpass" // <dev> in /remote/<dev>/... - the app must use the same id
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "" // older secrets.h: OTA disabled
#endif

// Hardware independent settings. The pinout is in the files under boards/.
namespace settings
{
    constexpr const char *kFirmwareName = "BrewPass";
    constexpr const char *kFirmwareVersion = "2.1.0";
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
    constexpr uint32_t kCloudTaskStackBytes = 16384;
    constexpr uint8_t kCloudTaskPriority = 1;
    constexpr uint8_t kCloudTaskCore = 0;
    constexpr uint8_t kCloudRequestQueueLen = 16;
    constexpr uint8_t kCloudResponseQueueLen = 8;
    constexpr uint8_t kLogMaxAttempts = 3;

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
    constexpr uint32_t kRemoteStreamMinFreeHeap = 70000;
    constexpr uint32_t kRemoteStreamHeapRetryMs = 60000;

    // Diagnostics
    constexpr uint32_t kHeapReportIntervalMs = 60000;
} // namespace settings
