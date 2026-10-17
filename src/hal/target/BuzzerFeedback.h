#pragma once

#include "../common/PatternFeedback.h"

// Passive buzzer driven by LEDC PWM (non-blocking, replaces the old tone()+delay())
class BuzzerFeedback : public PatternFeedback
{
public:
    BuzzerFeedback(uint8_t pin, uint8_t ledcChannel) : pin_(pin), channel_(ledcChannel) {}

    void begin() override
    {
        ledcSetup(channel_, 2000, 8);
        ledcAttachPin(pin_, channel_);
        ledcWriteTone(channel_, 0);
    }

protected:
    void toneOn(uint16_t freqHz) override { ledcWriteTone(channel_, freqHz); }
    void toneOff() override { ledcWriteTone(channel_, 0); }

private:
    uint8_t pin_;
    uint8_t channel_;
};
