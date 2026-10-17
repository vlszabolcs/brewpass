#pragma once

#include <Interfaces.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>

// WiFi connection handling with several networks in PRIORITY ORDER: tries the secrets.h WIFI_NETWORKS list
// in order (skipping networks that are not visible) and stops at the first success. Starts over after a drop.
// maintain() is called from the network task - so the blocking connect never stalls the main loop.
// The state getters may be called from any task.
class WifiNetwork : public INetwork
{
public:
    struct Credential
    {
        const char *ssid;
        const char *password;
    };

    WifiNetwork(const Credential *networks, size_t count);

    void begin();
    void maintain(uint32_t nowMs);

    bool connected() const override { return connected_.load(); }
    void ssid(char *out, size_t outSize) const override;
    int8_t rssi() const override { return rssi_.load(); }
    uint32_t connectionId() const override { return connectionId_.load(); }
    void statusDetail(char *out, size_t outSize) const override;

private:
    static const char *reasonText(uint8_t reason);
    void connectInOrder();

    static constexpr size_t kMaxNetworks = 8;

    const Credential *networks_;
    size_t count_;

    std::atomic<bool> connected_{false};
    std::atomic<int8_t> rssi_{0};
    std::atomic<uint8_t> lastReason_{0};
    std::atomic<uint32_t> attempts_{0};
    std::atomic<bool> noMatchVisible_{false};
    std::atomic<uint32_t> connectionId_{0};
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    char ssid_[33] = {0};

    bool wasConnected_ = false;
    uint32_t lastAttemptMs_ = 0;
    uint32_t lastRssiMs_ = 0;
    bool firstAttempt_ = true;
};
