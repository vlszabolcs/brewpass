#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ErrorInfo.h"

// Card UID (ISO14443A: 4, 7 or 10 bytes)
struct CardUid
{
    static constexpr size_t kMaxLength = 10;
    // 2 hex chars per byte + terminating NUL
    static constexpr size_t kHexBufferSize = kMaxLength * 2 + 1;

    uint8_t bytes[kMaxLength] = {0};
    uint8_t length = 0;

    bool isValid() const { return length > 0 && length <= kMaxLength; }

    // Upper-case hex without separators - same format as the original rfidChecker
    // (this is the user key in the database), e.g. "04A1B2C3".
    void toHex(char *out, size_t outSize) const
    {
        if (outSize == 0)
            return;
        out[0] = '\0';
        size_t pos = 0;
        for (uint8_t i = 0; i < length && pos + 2 < outSize; i++)
        {
            snprintf(out + pos, outSize - pos, "%02X", bytes[i]);
            pos += 2;
        }
    }

    bool operator==(const CardUid &other) const
    {
        return length == other.length && memcmp(bytes, other.bytes, length) == 0;
    }
    bool operator!=(const CardUid &other) const { return !(*this == other); }
};

// User data stored in the database (users/<UID>)
struct UserRecord
{
    char uid[CardUid::kHexBufferSize] = {0};
    char name[40] = {0};
    int32_t credit = 0;
    bool loan = false; // read, but the loan logic is not used yet
    uint32_t updated = 0;
};

// Device configuration (conf/price, conf/free)
struct DeviceConfig
{
    int32_t price = 0;
    bool freeMode = false;
    bool loaded = false; // loaded successfully at least once (from the cloud or the cache)

    bool priceValid() const { return price > 0; }
    bool operator==(const DeviceConfig &o) const { return price == o.price && freeMode == o.freeMode && loaded == o.loaded; }
    bool operator!=(const DeviceConfig &o) const { return !(*this == o); }
};

// ---------------------------------------------------------------- Remote purchase ("virtual card tap")
// The app writes /remote/<deviceId>/req/<id> = {cardUid, ts}, the device runs it through the same purchase
// flow as a physical card and reports progress in /remote/<deviceId>/status/<id>. Format: README.md.

// RTDB push keys are 20 characters
constexpr size_t kRemoteIdSize = 32;

// Progress of a remote purchase, written to status/<id>/state
enum class RemoteState : uint8_t
{
    None,     // nothing to write (only the request is deleted)
    Accepted, // "accepted": taken over, user being checked
    Charged,  // "charged": credit deducted, credit pulses being sent
    Done,     // "done": the machine finished the drink
    Error,    // "error": see the code (E-number); credit is present if the account was already charged
};

inline const char *remoteStateName(RemoteState s)
{
    switch (s)
    {
    case RemoteState::None: return "";
    case RemoteState::Accepted: return "accepted";
    case RemoteState::Charged: return "charged";
    case RemoteState::Done: return "done";
    case RemoteState::Error: return "error";
    }
    return "";
}

// The last handled remote request (server timestamp + push key). Requests at or before it are not run again,
// even if the stream re-delivers them (restart, reboot) - saved to NVS so a reboot cannot charge twice.
struct RemoteMark
{
    uint64_t tsMs = 0;
    char id[kRemoteIdSize] = {0};

    // true if (ts, id) was already handled
    bool covers(uint64_t ts, const char *reqId) const
    {
        return ts < tsMs || (ts == tsMs && strcmp(reqId, id) <= 0);
    }
};

// Log events.
// A purchase is logged together with the credit update: users/<UID>/log/<epoch> = remaining credit.
// A separate log write only exists for free purchases (daily counter: stat/free/<YYYYMMDD>).
// Failed purchases are not written to the database (serial log only).
enum class LogAction : int32_t
{
    Purchase = 0,
    FailedPurchase = 1,
    FreePurchase = 4, // the original code wrote 4 (its comment said 03) - 4 is kept
};
