#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "NetTaskClient.h"

// Over-the-air firmware update (ArduinoOTA / espota).
//
//  - Runs from the network task (INetTaskClient), so the main loop can keep updating the display.
//  - Only accepts an update when the application allows it (setAllowed) - never during a purchase.
//  - A password is mandatory (secrets.h: OTA_PASSWORD); with an empty password OTA is disabled.
//  - During the update the network task is removed from the watchdog (the upload blocks for ~20-40 s).
class OtaService : public INetTaskClient
{
public:
    OtaService(const char *hostname, const char *password, uint16_t port);

    void onNetTick(bool online) override;

    void setAllowed(bool allowed) { allowed_.store(allowed); }
    bool active() const { return active_.load(); }
    uint8_t progress() const { return progress_.load(); }
    bool enabled() const { return password_[0] != '\0'; }

    // After a successful update an NVS flag is written before the restart - the next boot knows from it
    // that it started after OTA (true once, then cleared).
    static bool consumeUpdatedFlag();
    // Reason of the last failed update (empty if none); cleared when read
    bool consumeLastError(char *out, size_t outSize);

private:
    void start();

    const char *hostname_;
    const char *password_;
    uint16_t port_;

    bool started_ = false;
    bool warned_ = false;
    std::atomic<bool> allowed_{false};
    std::atomic<bool> active_{false};
    std::atomic<uint8_t> progress_{0};
    std::atomic<int> lastError_{-1};
};
