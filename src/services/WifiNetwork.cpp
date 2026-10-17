#include "WifiNetwork.h"

#include <Log.h>
#include <esp_task_wdt.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "WIFI";
    WifiNetwork *gInstance = nullptr;
} // namespace

WifiNetwork::WifiNetwork(const Credential *networks, size_t count) : networks_(networks), count_(count) {}

void WifiNetwork::begin()
{
    gInstance = this;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(settings::kHostname);
    WiFi.setAutoReconnect(false); // reconnecting is done by maintain(), in the configured order
    WiFi.setSleep(false);         // more stable response time

    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
            if (gInstance != nullptr)
                gInstance->lastReason_.store(info.wifi_sta_disconnected.reason);
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    size_t configured = 0;
    for (size_t i = 0; i < count_; i++)
    {
        if (networks_[i].ssid == nullptr || networks_[i].ssid[0] == '\0')
            continue;
        configured++;
        LOGI(TAG, "Network %u: %s", (unsigned)configured, networks_[i].ssid);
    }
    if (configured == 0)
        LOGE(TAG, "No WiFi network configured (secrets.h: WIFI_NETWORKS)!");
}

void WifiNetwork::maintain(uint32_t nowMs)
{
    const bool up = WiFi.status() == WL_CONNECTED;

    if (up)
    {
        if (!wasConnected_)
        {
            wasConnected_ = true;
            // WiFi.SSID() may block (semaphore) - it must be queried OUTSIDE the critical section
            const String current = WiFi.SSID();
            portENTER_CRITICAL(&lock_);
            snprintf(ssid_, sizeof(ssid_), "%s", current.c_str());
            portEXIT_CRITICAL(&lock_);
            rssi_.store(static_cast<int8_t>(WiFi.RSSI()));
            attempts_.store(0);
            noMatchVisible_.store(false);
            connectionId_.fetch_add(1);
            LOGI(TAG, "Csatlakozva: %s, IP: %s, RSSI: %d dBm", current.c_str(), WiFi.localIP().toString().c_str(),
                 WiFi.RSSI());
        }
        if (nowMs - lastRssiMs_ >= settings::kWifiRssiRefreshMs)
        {
            lastRssiMs_ = nowMs;
            rssi_.store(static_cast<int8_t>(WiFi.RSSI()));
        }
        connected_.store(true);
        return;
    }

    if (wasConnected_)
    {
        wasConnected_ = false;
        const uint8_t reason = lastReason_.load();
        LOGW(TAG, "Kapcsolat megszakadt (ok: %u - %s)", reason, reasonText(reason));
    }
    connected_.store(false);

    if (firstAttempt_ || nowMs - lastAttemptMs_ >= settings::kWifiRetryIntervalMs)
    {
        firstAttempt_ = false;
        attempts_.fetch_add(1);
        LOGI(TAG, "Connecting... (round %lu)", (unsigned long)attempts_.load());
        connectInOrder(); // blocks (scan + attempts), but only the network task
        lastAttemptMs_ = millis();
    }
}

// Scans, then tries the configured networks in the given order; stops at the first success.
// Networks that are not visible are skipped - unless none is visible (then all are tried, for hidden SSIDs).
void WifiNetwork::connectInOrder()
{
    const bool verbose = (attempts_.load() % 6 == 1); // list of visible networks in round 1 and every 6th round
    const int found = WiFi.scanNetworks(false, true);
    esp_task_wdt_reset();

    bool visible[kMaxNetworks] = {false};
    bool anyVisible = false;
    if (found > 0)
    {
        if (verbose)
            LOGI(TAG, "Visible networks (%d):", found);
        for (int i = 0; i < found; i++)
        {
            const String seen = WiFi.SSID(i);
            bool configured = false;
            for (size_t k = 0; k < count_ && k < kMaxNetworks; k++)
            {
                if (networks_[k].ssid != nullptr && networks_[k].ssid[0] != '\0' && seen == networks_[k].ssid)
                {
                    visible[k] = true;
                    configured = true;
                    anyVisible = true;
                }
            }
            if (verbose && i < 15)
                LOGI(TAG, "  %c %-32s ch%-2d %4d dBm", configured ? '*' : ' ', seen.c_str(), WiFi.channel(i), WiFi.RSSI(i));
        }
    }
    else
    {
        LOGW(TAG, "Scan: no 2.4 GHz network visible at all (%d)", found);
    }
    WiFi.scanDelete();

    if (!anyVisible)
        LOGW(TAG, "None of the configured networks is visible (5 GHz? hidden? range?) - trying all of them");
    noMatchVisible_.store(!anyVisible);

    for (size_t k = 0; k < count_ && k < kMaxNetworks; k++)
    {
        const Credential &net = networks_[k];
        if (net.ssid == nullptr || net.ssid[0] == '\0')
            continue;
        if (anyVisible && !visible[k])
        {
            LOGI(TAG, "%u. %s: not visible, skipped", (unsigned)(k + 1), net.ssid);
            continue;
        }

        LOGI(TAG, "%u. %s: trying...", (unsigned)(k + 1), net.ssid);
        lastReason_.store(0);
        WiFi.disconnect(false, false);
        WiFi.begin(net.ssid, net.password);

        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < settings::kWifiConnectTimeoutMs)
        {
            esp_task_wdt_reset();
            delay(100);
        }
        if (WiFi.status() == WL_CONNECTED)
            return;

        const uint8_t reason = lastReason_.load();
        LOGW(TAG, "%u. %s: failed (reason: %u - %s)", (unsigned)(k + 1), net.ssid, reason, reasonText(reason));
    }
    WiFi.disconnect(false, false);
}

void WifiNetwork::ssid(char *out, size_t outSize) const
{
    portENTER_CRITICAL(&lock_);
    snprintf(out, outSize, "%s", ssid_);
    portEXIT_CRITICAL(&lock_);
}

void WifiNetwork::statusDetail(char *out, size_t outSize) const
{
    const uint8_t reason = lastReason_.load();
    const uint32_t attempts = attempts_.load();
    if (noMatchVisible_.load())
        snprintf(out, outSize, "Configured network not visible (5 GHz? hidden? range?), attempt %lu",
                 (unsigned long)attempts);
    else if (reason != 0)
        snprintf(out, outSize, "attempt %lu, last reason: %s (%u)", (unsigned long)attempts, reasonText(reason), reason);
    else if (attempts > 0)
        snprintf(out, outSize, "Connecting... (attempt %lu)", (unsigned long)attempts);
    else if (outSize > 0)
        out[0] = '\0';
}

const char *WifiNetwork::reasonText(uint8_t reason)
{
    switch (reason)
    {
    case 0: return "-";
    case 2: return "auth expired";
    case 4: return "inactivity";
    case 8: return "AP bontott";
    case 15: return "4-way handshake timeout (wrong password?)";
    case 200: return "beacon timeout (weak signal)";
    case 201: return "no such network";
    case 202: return "auth failed (password?)";
    case 203: return "association failed";
    case 204: return "handshake timeout (password?)";
    case 205: return "connection failed";
    default: return "other";
    }
}
