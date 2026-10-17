#pragma once

#include <cstdint>

#include "Types.h"

// Filters repeated detections of the same card.
// The PN532 keeps reporting a card left on the reader, the RC522 does not (because of HaltA) -
// the filtering is the same for both: the same UID only counts as a new tap if it was not seen
// for at least holdOffMs.
class CardDebouncer
{
public:
    explicit CardDebouncer(uint32_t holdOffMs) : holdOffMs_(holdOffMs) {}

    // true if the card has to be handled as a new tap
    bool accept(const CardUid &uid, uint32_t nowMs)
    {
        const bool same = hasLast_ && uid == last_;
        const bool expired = (nowMs - lastSeenMs_) >= holdOffMs_;
        last_ = uid;
        hasLast_ = true;
        lastSeenMs_ = nowMs;
        return !same || expired;
    }

    void reset() { hasLast_ = false; }

private:
    uint32_t holdOffMs_;
    CardUid last_;
    bool hasLast_ = false;
    uint32_t lastSeenMs_ = 0;
};
