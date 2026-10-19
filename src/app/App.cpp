#include "App.h"

#include <Log.h>
#include <Messages.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "BoardConfig.h"
#include "Settings.h"

namespace
{
    constexpr const char *TAG = "APP";

    const WifiNetwork::Credential kNetworks[] = WIFI_NETWORKS;
    constexpr uint32_t kBootEventTtlMs = 30UL * 60UL * 1000UL; // also logged after an offline boot if the network is up within 30 min

    // survives a software restart: marks the restart made by the stuck database connection recovery
    constexpr uint32_t kCloudRestartMagic = 0xC10DB007;
    RTC_NOINIT_ATTR uint32_t gCloudRestartMark;

    bool consumeCloudRestartMark()
    {
        const bool set = esp_reset_reason() == ESP_RST_SW && gCloudRestartMark == kCloudRestartMagic;
        gCloudRestartMark = 0;
        return set;
    }

    const char *resetReasonText(esp_reset_reason_t r)
    {
        switch (r)
        {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_EXT: return "external reset";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "PANIC (exception/error)";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog (hang)";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "SDIO";
        default: return "unknown";
        }
    }
} // namespace

App::App()
    : hw_(board::hardware()), wifi_(kNetworks, sizeof(kNetworks) / sizeof(kNetworks[0])), cloud_(wifi_, clock_),
      ota_(settings::kHostname, OTA_PASSWORD, settings::kOtaPort),
      controller_(hw_.reader, hw_.coin, hw_.ui, hw_.feedback, cloud_, wifi_, clock_)
{
}

void App::begin()
{
    printBanner();
    configureWatchdog();

    if (!hw_.ui.begin())
        LOGE(TAG, "Display init failed");
    hw_.feedback.begin();
    hw_.coin.begin();
    if (!hw_.reader.begin())
        LOGE(TAG, "RFID reader (%s) not responding - retrying in the background", hw_.reader.name());

    cloud_.setNetTaskClient(&ota_);
    cloud_.begin(); // WiFi + time + Firebase + OTA in the background task

    // boot event to the device log (/Log); the background task writes it after WiFi + time sync
    const bool afterOta = OtaService::consumeUpdatedFlag();
    const bool afterCloudStuck = consumeCloudRestartMark();
    const CloudRequest boot = makeDeviceEvent(millis() + kBootEventTtlMs, 0, "boot", "%s v%s, reason: %s%s%s", board::kShortName,
                                              settings::kFirmwareVersion, resetReasonText(esp_reset_reason()),
                                              afterOta ? " (after OTA update)" : "",
                                              afterCloudStuck ? " (database requests kept failing)" : "");
    cloud_.submit(boot);

    controller_.begin(configStore_.load(), configStore_.loadRemoteMark());
    hw_.feedback.play(FeedbackPattern::Startup);
    LOGI(TAG, "Startup done, free heap: %u bytes", (unsigned)ESP.getFreeHeap());
}

void App::loop()
{
    const uint32_t now = millis();
    hw_.coin.loop(now);
    hw_.feedback.loop(now);
    if (handleOta())
    {
        hw_.ui.loop(now);
        return;
    }
    controller_.loop();
    if (controller_.consumeConfigChanged())
        configStore_.save(controller_.config());
    if (controller_.consumeRemoteMarkChanged())
        configStore_.saveRemoteMark(controller_.remoteMark());
    hw_.ui.loop(now);
    reportHealth(now);
    restartIfCloudStuck();
}

// Last resort for a database connection that does not recover by itself (a reset always fixed it)
void App::restartIfCloudStuck()
{
    const VendController::State st = controller_.state();
    if (!cloud_.stuck() || (st != VendController::State::Idle && st != VendController::State::NotReady))
        return;
    LOGE(TAG, "Database requests keep failing for %lu s - restarting",
         (unsigned long)(settings::kCloudStuckRestartMs / 1000));
    gCloudRestartMark = kCloudRestartMagic;
    delay(100);
    ESP.restart();
}

// true while an OTA update is running (purchases are paused meanwhile)
bool App::handleOta()
{
    if (ota_.active())
    {
        const uint8_t p = ota_.progress();
        if (!otaShown_ || p != otaShownProgress_)
        {
            otaShown_ = true;
            otaShownProgress_ = p;
            Screen s;
            s.kind = ScreenKind::Boot;
            snprintf(s.title, sizeof(s.title), "%s", msg::kOtaTitle);
            snprintf(s.text, sizeof(s.text), "%u%% %s", p, msg::kOtaProgressSuffix);
            hw_.ui.show(s);
        }
        return true;
    }
    if (otaShown_)
    {
        // interrupted update: back to normal operation
        otaShown_ = false;
        otaShownProgress_ = 0xFF;
        controller_.refreshScreen();
    }
    char otaError[64];
    if (ota_.consumeLastError(otaError, sizeof(otaError)))
        cloud_.submit(makeDeviceEvent(millis() + kBootEventTtlMs, 0, "ota", "sikertelen: %s", otaError));
    // an update may only start in an idle state
    const VendController::State st = controller_.state();
    ota_.setAllowed(st == VendController::State::Idle || st == VendController::State::NotReady);
    return false;
}

void App::configureWatchdog()
{
    // The task watchdog watches both the main loop and the network task. It restarts on a hang;
    // the restart reason is printed at the next boot (printBanner).
    esp_task_wdt_init(settings::kWatchdogTimeoutS, true);
    enableLoopWDT();
}

void App::printBanner()
{
    LOGI(TAG, "==============================================");
    LOGI(TAG, "%s v%s", settings::kFirmwareName, settings::kFirmwareVersion);
    LOGI(TAG, "Hardver: %s", board::kName);
    LOGI(TAG, "Reset reason: %s", resetReasonText(esp_reset_reason()));
    LOGI(TAG, "Chip: %s rev%d, %u MHz, heap: %u, PSRAM: %u", ESP.getChipModel(), ESP.getChipRevision(),
         (unsigned)ESP.getCpuFreqMHz(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize());
    LOGI(TAG, "==============================================");
}

void App::reportHealth(uint32_t nowMs)
{
    if (nowMs - lastHealthMs_ < settings::kHeapReportIntervalMs)
        return;
    lastHealthMs_ = nowMs;
    LOGI(TAG, "State: %s | heap: %u (min %u, largest block %u) | uptime: %lu s",
         VendController::stateName(controller_.state()), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
         (unsigned)ESP.getMaxAllocHeap(), (unsigned long)(nowMs / 1000));
}
