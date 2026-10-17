#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "ErrorInfo.h"
#include "Types.h"

// Abstractions of the hardware and service layers. VendController reaches the outside world only
// through these, so the same logic runs on the target hardware, the test hardware and in native tests.

// ---------------------------------------------------------------- RFID reader
class ICardReader
{
public:
    virtual ~ICardReader() = default;
    virtual bool begin() = 0;
    // Non-blocking. true if a new card was read (the UID is written to out).
    // The reader also performs its own health check / recovery here.
    virtual bool poll(CardUid &out) = 0;
    virtual bool healthy() const = 0;
    virtual const ErrorInfo &lastError() const = 0;
    virtual const char *name() const = 0;
};

// ---------------------------------------------------------------- Coffee machine interface (coin validator emulation)
class ICoinMechanism
{
public:
    virtual ~ICoinMechanism() = default;
    virtual void begin() = 0;
    virtual void loop(uint32_t nowMs) = 0;
    // true if the machine accepts credit (inhibit signal LOW, debounced)
    virtual bool acceptingCredit() const = 0;
    // Starts the credit pulse train (non-blocking). false if one is already running.
    virtual bool startCreditPulses() = 0;
    // true while the pulse train is running
    virtual bool pulsing() const = 0;
    virtual const char *name() const = 0;
};

// ---------------------------------------------------------------- Sound / light feedback
enum class FeedbackPattern : uint8_t
{
    Startup,
    CardAccepted,
    Success,
    Error,
};

class IFeedback
{
public:
    virtual ~IFeedback() = default;
    virtual void begin() = 0;
    virtual void play(FeedbackPattern pattern) = 0; // non-blocking
    virtual void loop(uint32_t nowMs) = 0;
};

// ---------------------------------------------------------------- Display
enum class ScreenKind : uint8_t
{
    Boot,     // starting / system not ready (title + text)
    Idle,     // "Fizess!" (pay prompt)
    FreeIdle, // free mode
    Checking, // card read, database query in progress
    Success,  // credit display (amount)
    FreeVend, // free credit sent
    Error,    // error (error)
};

struct Screen
{
    ScreenKind kind = ScreenKind::Boot;
    char title[48] = {0};
    char text[96] = {0};
    bool hasAmount = false;
    int32_t amount = 0;
    ErrorInfo error;
};

struct StatusInfo
{
    bool wifi = false;
    char ssid[33] = {0};
    char ip[16] = {0}; // "192.168.50.101", empty if not connected
    int8_t rssi = 0;
    bool cloud = false;
    bool timeSynced = false;
    char clock[9] = {0}; // "HH:MM:SS"
    bool machineReady = false;
    bool freeMode = false;
    int32_t price = 0;
    bool liveConfig = false; // config changes are watched by a live stream

    bool operator==(const StatusInfo &o) const;
    bool operator!=(const StatusInfo &o) const { return !(*this == o); }
};

class IUi
{
public:
    virtual ~IUi() = default;
    virtual bool begin() = 0;
    virtual void show(const Screen &screen) = 0;
    virtual void setStatus(const StatusInfo &status) = 0;
    // Redraws if anything changed (called from the main loop)
    virtual void loop(uint32_t nowMs) = 0;
};

// ---------------------------------------------------------------- Network, time
class INetwork
{
public:
    virtual ~INetwork() = default;
    virtual bool connected() const = 0;
    virtual void ssid(char *out, size_t outSize) const = 0;
    virtual int8_t rssi() const = 0;
    // Current IP address as text ("" if not connected)
    virtual void ip(char *out, size_t outSize) const = 0;
    // Incremented on every successful (re)connection - tells the controller that a full config fetch is due
    virtual uint32_t connectionId() const = 0;
    // Human-readable state (e.g. last disconnect reason) for detailed error reporting; empty if none
    virtual void statusDetail(char *out, size_t outSize) const = 0;
};

class IClock
{
public:
    virtual ~IClock() = default;
    virtual uint32_t nowMs() const = 0;
    virtual bool synced() const = 0;
    virtual uint32_t epoch() const = 0; // UTC seconds
    virtual void formatLocalTime(char *out, size_t outSize) const = 0; // "HH:MM:SS"
};

// ---------------------------------------------------------------- Cloud (Firebase RTDB), asynchronous request/response
enum class CloudOp : uint8_t
{
    FetchUser,
    UpdateUser,
    WriteLog,
    FetchConfig,
    ConfigChanged, // unsolicited (stream) config change - only appears as a response
    DeviceEvent,   // device status log: /Log/<epoch> = {"e": event, "d": detail}
    RemoteVend,    // unsolicited (stream) purchase request from the app - only appears as a response
    RemoteStatus,  // remote purchase progress (status/<id>) and/or deletion of the request (req/<id>)
    RemoteOnline,  // remote purchase availability heartbeat (online)
};

struct CloudRequest
{
    uint32_t id = 0;
    CloudOp op = CloudOp::FetchUser;
    // Valid until this time (millis). The worker does NOT execute an expired request - so the account can
    // never be charged in the background after the controller has already given up the transaction.
    uint32_t deadlineMs = 0;
    char uid[CardUid::kHexBufferSize] = {0};
    int32_t credit = 0;
    uint32_t timestamp = 0;
    LogAction action = LogAction::Purchase;
    uint8_t attempt = 0; // number of retries (logs)
    // DeviceEvent
    char event[12] = {0};
    char detail[96] = {0};
    // RemoteStatus (credit: the credit after the charge, only written if charged)
    char remoteId[kRemoteIdSize] = {0};
    RemoteState remoteState = RemoteState::None;
    bool deleteRemoteRequest = false;
    bool charged = false;
    uint16_t errorCode = 0;
    // RemoteOnline
    bool remoteReady = false;
    bool freeMode = false;
    int32_t price = 0;
};

// Builds a device status event (timestamp = 0: the worker fills it in when writing)
inline CloudRequest makeDeviceEvent(uint32_t deadlineMs, uint32_t timestamp, const char *event, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
inline CloudRequest makeDeviceEvent(uint32_t deadlineMs, uint32_t timestamp, const char *event, const char *fmt, ...)
{
    CloudRequest req;
    req.op = CloudOp::DeviceEvent;
    req.deadlineMs = deadlineMs;
    req.timestamp = timestamp;
    snprintf(req.event, sizeof(req.event), "%s", event);
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.detail, sizeof(req.detail), fmt, args);
    va_end(args);
    return req;
}

struct CloudResponse
{
    uint32_t id = 0;
    CloudOp op = CloudOp::FetchUser;
    bool ok = false;
    bool notFound = false;
    ErrorInfo error;
    UserRecord user;
    DeviceConfig config;
    // For ConfigChanged: which fields were part of the change
    bool hasPrice = false;
    bool hasFree = false;
    // RemoteVend: push key and server timestamp (ms) of the request; the card UID is in user.uid.
    // ok=false: malformed request (error tells why), it is refused and deleted.
    char remoteId[kRemoteIdSize] = {0};
    uint64_t remoteTsMs = 0;
};

class ICloud
{
public:
    virtual ~ICloud() = default;
    // Signed in and able to serve requests
    virtual bool ready() const = 0;
    // Non-blocking; false if the queue is full
    virtual bool submit(const CloudRequest &request) = 0;
    // Non-blocking; true if there was a response
    virtual bool poll(CloudResponse &response) = 0;
    // Last general (e.g. sign-in) error - for the status display
    virtual ErrorInfo statusError() const = 0;
    // true if config changes are watched by a live connection (stream)
    virtual bool liveUpdates() const { return false; }
    // true if app purchase requests are watched by a live connection (stream)
    virtual bool remoteLive() const { return false; }
};

inline bool StatusInfo::operator==(const StatusInfo &o) const
{
    return wifi == o.wifi && rssi == o.rssi && cloud == o.cloud && timeSynced == o.timeSynced &&
           machineReady == o.machineReady && freeMode == o.freeMode && price == o.price && liveConfig == o.liveConfig &&
           strcmp(ssid, o.ssid) == 0 && strcmp(ip, o.ip) == 0 && strcmp(clock, o.clock) == 0;
}
