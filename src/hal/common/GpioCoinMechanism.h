#pragma once

#include <Arduino.h>
#include <Interfaces.h>
#include <atomic>
#include <esp_timer.h>

// Real coffee machine interface (NRI G-13 emulation):
//  - COIN output: active LOW pulses. Timing comes from esp_timer, so main-loop load
//    (display refresh, network) does not distort the pulse width.
//  - inhibit input: LOW = the machine accepts credit. Debounced.
class GpioCoinMechanism : public ICoinMechanism
{
public:
    struct PulseConfig
    {
        uint8_t count;
        uint32_t lowMs;
        uint32_t highMs;
    };

    GpioCoinMechanism(uint8_t coinOutPin, uint8_t inhibitPin, bool openDrain, const PulseConfig &pulses,
                      uint32_t debounceMs);

    void begin() override;
    void loop(uint32_t nowMs) override;
    bool acceptingCredit() const override { return stableLevel_ == LOW; }
    bool startCreditPulses() override;
    bool pulsing() const override { return pulsing_.load(); }
    const char *name() const override { return "GPIO (NRI G-13 emulation)"; }

private:
    static void onTimer(void *arg);
    void step();

    const uint8_t outPin_;
    const uint8_t inhibitPin_;
    const bool openDrain_;
    const PulseConfig pulses_;
    const uint32_t debounceMs_;

    esp_timer_handle_t timer_ = nullptr;
    std::atomic<bool> pulsing_{false};
    uint8_t edgesLeft_ = 0; // remaining edges (2 per pulse)
    bool outLow_ = false;

    int rawLevel_ = HIGH;
    int stableLevel_ = HIGH;
    uint32_t rawChangedMs_ = 0;
};
