#pragma once

#include <Arduino.h>

// Debounced push button with short / long / very long press detection (active LOW, internal pull-up).
//
//  - veryLongPressMs == 0: the Long event fires while still held, at longPressMs.
//  - veryLongPressMs  > 0: the Long event fires on RELEASE (if longPressMs <= press < veryLongPressMs),
//    and VeryLong fires while held, when veryLongPressMs is reached - so the two never collide.
class Button
{
public:
    enum class Event : uint8_t
    {
        None,
        Short,
        Long,
        VeryLong,
    };

    Button(uint8_t pin, uint32_t longPressMs, uint32_t veryLongPressMs = 0, uint32_t debounceMs = 30)
        : pin_(pin), longPressMs_(longPressMs), veryLongPressMs_(veryLongPressMs), debounceMs_(debounceMs)
    {
    }

    void begin()
    {
        pinMode(pin_, INPUT_PULLUP);
        raw_ = stable_ = digitalRead(pin_);
    }

    bool pressed() const { return stable_ == LOW; }

    Event update(uint32_t nowMs)
    {
        const int level = digitalRead(pin_);
        if (level != raw_)
        {
            raw_ = level;
            changedMs_ = nowMs;
            return Event::None;
        }
        if (level != stable_ && nowMs - changedMs_ >= debounceMs_)
        {
            stable_ = level;
            if (stable_ == LOW)
            {
                pressedMs_ = nowMs;
                fired_ = false;
            }
            else if (!fired_)
            {
                const uint32_t held = nowMs - pressedMs_;
                if (veryLongPressMs_ > 0 && held >= longPressMs_)
                    return Event::Long;
                return Event::Short;
            }
        }
        if (stable_ == LOW && !fired_)
        {
            const uint32_t held = nowMs - pressedMs_;
            if (veryLongPressMs_ > 0 && held >= veryLongPressMs_)
            {
                fired_ = true;
                return Event::VeryLong;
            }
            if (veryLongPressMs_ == 0 && held >= longPressMs_)
            {
                fired_ = true;
                return Event::Long;
            }
        }
        return Event::None;
    }

private:
    uint8_t pin_;
    uint32_t longPressMs_;
    uint32_t veryLongPressMs_;
    uint32_t debounceMs_;
    int raw_ = HIGH;
    int stable_ = HIGH;
    uint32_t changedMs_ = 0;
    uint32_t pressedMs_ = 0;
    bool fired_ = false;
};
