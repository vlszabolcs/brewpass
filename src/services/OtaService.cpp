#include "OtaService.h"

#include <ArduinoOTA.h>
#include <Log.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "OTA";

    const char *otaErrorText(ota_error_t e)
    {
        switch (e)
        {
        case OTA_AUTH_ERROR: return "auth error (wrong password?)";
        case OTA_BEGIN_ERROR: return "begin error (not enough space in the partition?)";
        case OTA_CONNECT_ERROR: return "connect error";
        case OTA_RECEIVE_ERROR: return "receive error (transfer interrupted)";
        case OTA_END_ERROR: return "end error (corrupt image?)";
        }
        return "unknown error";
    }
} // namespace

OtaService::OtaService(const char *hostname, const char *password, uint16_t port)
    : hostname_(hostname), password_(password != nullptr ? password : ""), port_(port)
{
}

void OtaService::start()
{
    ArduinoOTA.setHostname(hostname_);
    ArduinoOTA.setPort(port_);
    ArduinoOTA.setPassword(password_);
    ArduinoOTA.setRebootOnSuccess(true);

    ArduinoOTA.onStart([this]() {
        active_.store(true);
        progress_.store(0);
        // the upload blocks the network task - the watchdog must not reset it
        esp_task_wdt_delete(nullptr);
        LOGW(TAG, "Update starting (%s)", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
    });
    ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
        const uint8_t p = total > 0 ? static_cast<uint8_t>((static_cast<uint64_t>(done) * 100U) / total) : 0;
        if (p != progress_.load() && p % 10 == 0)
            LOGI(TAG, "%u%%", p);
        progress_.store(p);
    });
    ArduinoOTA.onEnd([]() {
        Preferences prefs;
        if (prefs.begin(settings::kNvsNamespace, false))
        {
            prefs.putBool("ota_done", true);
            prefs.end();
        }
        LOGI(TAG, "Update done, restarting...");
    });
    ArduinoOTA.onError([this](ota_error_t e) {
        LOGE(TAG, "Update failed: %s (%d)", otaErrorText(e), static_cast<int>(e));
        esp_task_wdt_add(nullptr);
        lastError_.store(static_cast<int>(e));
        active_.store(false);
    });

    ArduinoOTA.begin();
    started_ = true;
    LOGI(TAG, "OTA ready: %s.local / %s, port %u", hostname_, WiFi.localIP().toString().c_str(), port_);
}

bool OtaService::consumeUpdatedFlag()
{
    Preferences prefs;
    if (!prefs.begin(settings::kNvsNamespace, false))
        return false;
    const bool done = prefs.getBool("ota_done", false);
    if (done)
        prefs.remove("ota_done");
    prefs.end();
    return done;
}

bool OtaService::consumeLastError(char *out, size_t outSize)
{
    const int e = lastError_.exchange(-1);
    if (e < 0)
        return false;
    snprintf(out, outSize, "%s (%d)", otaErrorText(static_cast<ota_error_t>(e)), e);
    return true;
}

void OtaService::onNetTick(bool online)
{
    if (!online)
        return;
    if (!enabled())
    {
        if (!warned_)
        {
            warned_ = true;
            LOGW(TAG, "OTA disabled: no OTA_PASSWORD in secrets.h");
        }
        return;
    }
    if (!started_)
        start();

    // If not allowed (a purchase is running) the incoming update request is not served -
    // espota then stops with "No response from device" and can be retried later.
    if (allowed_.load() || active_.load())
        ArduinoOTA.handle();
}
