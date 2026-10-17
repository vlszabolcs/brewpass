#include "GpioCoinMechanism.h"

#include <Log.h>

namespace
{
    constexpr const char *TAG = "COIN";
}

GpioCoinMechanism::GpioCoinMechanism(uint8_t coinOutPin, uint8_t inhibitPin, bool openDrain,
                                     const PulseConfig &pulses, uint32_t debounceMs)
    : outPin_(coinOutPin), inhibitPin_(inhibitPin), openDrain_(openDrain), pulses_(pulses), debounceMs_(debounceMs)
{
}

void GpioCoinMechanism::begin()
{
    // Level first, then direction - so there is no short LOW glitch on the COIN line at startup.
    digitalWrite(outPin_, HIGH);
    pinMode(outPin_, openDrain_ ? OUTPUT_OPEN_DRAIN : OUTPUT);
    digitalWrite(outPin_, HIGH);

    pinMode(inhibitPin_, INPUT);
    rawLevel_ = stableLevel_ = digitalRead(inhibitPin_);
    rawChangedMs_ = millis();

    esp_timer_create_args_t args = {};
    args.callback = &GpioCoinMechanism::onTimer;
    args.arg = this;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "coin_pulse";
    if (esp_timer_create(&args, &timer_) != ESP_OK)
        LOGE(TAG, "esp_timer creation failed - no pulses possible!");

    LOGI(TAG, "COIN=GPIO%u (%s), inhibit=GPIO%u, initial inhibit: %s", outPin_, openDrain_ ? "open-drain" : "push-pull",
         inhibitPin_, stableLevel_ == LOW ? "LOW (machine ready)" : "HIGH (machine busy)");
}

void GpioCoinMechanism::loop(uint32_t nowMs)
{
    const int level = digitalRead(inhibitPin_);
    if (level != rawLevel_)
    {
        rawLevel_ = level;
        rawChangedMs_ = nowMs;
    }
    else if (level != stableLevel_ && nowMs - rawChangedMs_ >= debounceMs_)
    {
        stableLevel_ = level;
        LOGI(TAG, "Inhibit -> %s", level == LOW ? "LOW (machine accepts credit)" : "HIGH (machine busy / has credit / fault)");
    }
}

bool GpioCoinMechanism::startCreditPulses()
{
    if (timer_ == nullptr || pulsing_.load())
        return false;
    LOGI(TAG, "Credit: %u pulses (%lu ms LOW / %lu ms HIGH)", pulses_.count, (unsigned long)pulses_.lowMs,
         (unsigned long)pulses_.highMs);
    edgesLeft_ = pulses_.count * 2;
    outLow_ = false;
    pulsing_.store(true);
    step();
    return true;
}

void GpioCoinMechanism::onTimer(void *arg)
{
    static_cast<GpioCoinMechanism *>(arg)->step();
}

void GpioCoinMechanism::step()
{
    if (edgesLeft_ == 0)
    {
        digitalWrite(outPin_, HIGH);
        pulsing_.store(false);
        return;
    }
    outLow_ = !outLow_;
    digitalWrite(outPin_, outLow_ ? LOW : HIGH);
    edgesLeft_--;
    // after the last rising edge the HIGH time is still waited out, so the end of the train is well defined
    const uint32_t waitMs = outLow_ ? pulses_.lowMs : pulses_.highMs;
    esp_timer_start_once(timer_, static_cast<uint64_t>(waitMs) * 1000ULL);
}
