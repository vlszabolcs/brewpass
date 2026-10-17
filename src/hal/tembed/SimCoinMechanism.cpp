#include "SimCoinMechanism.h"

#include <Log.h>

namespace
{
    constexpr const char *TAG = "SIM";
}

SimCoinMechanism::SimCoinMechanism(uint8_t vendDoneButtonPin, uint8_t machineButtonPin, uint32_t brewMs,
                                   uint32_t longPressMs, uint32_t powerOffHoldMs, uint8_t pulseCount, uint32_t pulseLowMs,
                                   uint32_t pulseHighMs)
    : vendDoneButton_(vendDoneButtonPin, longPressMs), machineButton_(machineButtonPin, longPressMs, powerOffHoldMs),
      brewMs_(brewMs),
      pulseCount_(pulseCount), pulseLowMs_(pulseLowMs), pulseHighMs_(pulseHighMs)
{
}

void SimCoinMechanism::begin()
{
    vendDoneButton_.begin();
    machineButton_.begin();
    LOGI(TAG, "Coffee machine simulation: encoder button = cup taken (machine ready again) | side button: short = machine "
              "busy on/off, 1-5 s = ignore pulses on/off, 5 s = power off");
}

bool SimCoinMechanism::startCreditPulses()
{
    if (edgesLeft_ > 0)
        return false;
    edgesLeft_ = pulseCount_ * 2;
    outLow_ = false;
    pulsesSeen_ = 0;
    nextEdgeMs_ = millis();
    return true;
}

void SimCoinMechanism::loop(uint32_t nowMs)
{
    // --- gombok
    switch (vendDoneButton_.update(nowMs))
    {
    case Button::Event::Short:
    case Button::Event::Long:
        if (brewing_)
        {
            brewing_ = false;
            LOGI(TAG, "Machine: cup taken (encoder button) -> inhibit LOW, machine ready again");
        }
        break;
    default:
        break;
    }

    switch (machineButton_.update(nowMs))
    {
    case Button::Event::Short:
        forcedBusy_ = !forcedBusy_;
        LOGI(TAG, "Machine: %s", forcedBusy_ ? "BUSY / FAULT (inhibit HIGH)" : "normal operation");
        break;
    case Button::Event::Long:
        ignorePulses_ = !ignorePulses_;
        LOGI(TAG, "Machine: credit pulses %s", ignorePulses_ ? "IGNORED" : "accepted");
        break;
    case Button::Event::VeryLong:
        LOGW(TAG, "Side button 5 s: power off requested");
        if (powerOffHandler_)
            powerOffHandler_();
        break;
    default:
        break;
    }

    // --- pulse train (simulated COIN output)
    if (edgesLeft_ > 0 && static_cast<int32_t>(nowMs - nextEdgeMs_) >= 0)
    {
        outLow_ = !outLow_;
        edgesLeft_--;
        if (outLow_)
        {
            pulsesSeen_++;
            LOGI(TAG, "COIN pulse #%u", pulsesSeen_);
        }
        nextEdgeMs_ = nowMs + (outLow_ ? pulseLowMs_ : pulseHighMs_);

        if (edgesLeft_ == 0)
        {
            if (ignorePulses_ || forcedBusy_)
            {
                LOGW(TAG, "Machine: credit NOT accepted");
            }
            else
            {
                brewing_ = true;
                brewEndMs_ = nowMs + brewMs_;
                if (brewMs_ == 0)
                    LOGI(TAG, "Machine: credit accepted -> inhibit HIGH (busy). Encoder button = cup taken, machine ready again");
                else
                    LOGI(TAG, "Machine: credit accepted -> inhibit HIGH, brewing %lu s", (unsigned long)(brewMs_ / 1000));
            }
        }
    }

    // --- end of brewing
    if (brewing_ && brewMs_ > 0 && static_cast<int32_t>(nowMs - brewEndMs_) >= 0)
    {
        brewing_ = false;
        LOGI(TAG, "Machine: drink ready -> inhibit LOW");
    }
}
