#pragma once

#include <Arduino.h>
#include <Interfaces.h>

// Non-blocking playback of sound patterns. A subclass only implements tone generation (and optionally
// light signalling) - the timing is shared. Replaces the old delay()-based beep functions.
class PatternFeedback : public IFeedback
{
public:
    struct Step
    {
        uint16_t freqHz; // 0 = silence
        uint16_t durationMs;
    };

    void play(FeedbackPattern pattern) override;
    void loop(uint32_t nowMs) override;

protected:
    virtual void toneOn(uint16_t freqHz) = 0;
    virtual void toneOff() = 0;
    // pattern start / end (e.g. for LED colouring)
    virtual void patternStarted(FeedbackPattern /*pattern*/) {}
    virtual void patternFinished() {}
    // feeding while a tone plays (e.g. filling the I2S buffer)
    virtual void service() {}

private:
    void applyStep();

    const Step *steps_ = nullptr;
    size_t stepCount_ = 0;
    size_t index_ = 0;
    uint32_t stepStartMs_ = 0;
    bool active_ = false;
};
