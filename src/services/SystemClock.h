#pragma once

#include <Arduino.h>
#include <Interfaces.h>
#include <atomic>
#include <ctime>

// System time via SNTP (the built-in ESP32 client, updated in the background) with a POSIX time zone.
// Replaces the old NTPClient + manual DST calculation at boot - so DST changes are correct at runtime too.
class SystemClock : public IClock
{
public:
    // From the network task, after the first WiFi connection
    void startSync(const char *tz, const char *server1, const char *server2)
    {
        if (started_.exchange(true))
            return;
        configTzTime(tz, server1, server2);
    }

    uint32_t nowMs() const override { return millis(); }

    bool synced() const override { return time(nullptr) > kValidEpoch; }

    uint32_t epoch() const override { return static_cast<uint32_t>(time(nullptr)); }

    void formatLocalTime(char *out, size_t outSize) const override
    {
        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        strftime(out, outSize, "%H:%M:%S", &local);
    }

private:
    static constexpr time_t kValidEpoch = 1700000000; // 2023-11 - any earlier time = not synced yet
    std::atomic<bool> started_{false};
};
