#include "PatternFeedback.h"

namespace
{
    // The original sounds: accept 600 Hz / 500 ms, error 3 x (1000 Hz / 300 ms, 300 ms pause)
    constexpr PatternFeedback::Step kAccept[] = {{600, 500}};
    constexpr PatternFeedback::Step kSuccess[] = {{1200, 80}, {0, 40}, {1600, 120}};
    constexpr PatternFeedback::Step kError[] = {{1000, 300}, {0, 300}, {1000, 300}, {0, 300}, {1000, 300}};

    template <size_t N>
    constexpr size_t count(const PatternFeedback::Step (&)[N]) { return N; }
} // namespace

void PatternFeedback::play(FeedbackPattern pattern)
{
    switch (pattern)
    {
    case FeedbackPattern::Startup:
    case FeedbackPattern::CardAccepted:
        steps_ = kAccept;
        stepCount_ = count(kAccept);
        break;
    case FeedbackPattern::Success:
        steps_ = kSuccess;
        stepCount_ = count(kSuccess);
        break;
    case FeedbackPattern::Error:
        steps_ = kError;
        stepCount_ = count(kError);
        break;
    }
    index_ = 0;
    stepStartMs_ = millis();
    active_ = true;
    patternStarted(pattern);
    applyStep();
}

void PatternFeedback::applyStep()
{
    const Step &s = steps_[index_];
    if (s.freqHz > 0)
        toneOn(s.freqHz);
    else
        toneOff();
}

void PatternFeedback::loop(uint32_t nowMs)
{
    if (!active_)
        return;

    service();
    if (nowMs - stepStartMs_ < steps_[index_].durationMs)
        return;

    index_++;
    stepStartMs_ = nowMs;
    if (index_ >= stepCount_)
    {
        active_ = false;
        toneOff();
        patternFinished();
        return;
    }
    applyStep();
}
