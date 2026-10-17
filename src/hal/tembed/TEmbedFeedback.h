#pragma once

#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>

#include "../common/PatternFeedback.h"

// T-Embed feedback: I2S speaker (square wave, non-blocking buffer filling) + WS2812 LED ring
// (blue = card accepted, green = success, red = error).
class TEmbedFeedback : public PatternFeedback
{
public:
    TEmbedFeedback(uint8_t bclkPin, uint8_t lrclkPin, uint8_t doutPin, uint8_t ledPin, uint8_t ledCount,
                   uint8_t ledBrightness, bool soundEnabled);

    void begin() override;
    // all lights and sound off (before power off)
    void allOff();

protected:
    void toneOn(uint16_t freqHz) override;
    void toneOff() override;
    void patternStarted(FeedbackPattern pattern) override;
    void patternFinished() override;
    void service() override;

private:
    void fill(uint32_t color);

    uint8_t bclkPin_, lrclkPin_, doutPin_;
    Adafruit_NeoPixel leds_;
    uint8_t ledBrightness_;
    bool soundEnabled_;
    bool i2sOk_ = false;
    uint16_t freqHz_ = 0;
    uint32_t phase_ = 0;
};
