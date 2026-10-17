#pragma once

#include <cstdint>

#include "CardDebouncer.h"
#include "ErrorInfo.h"
#include "Interfaces.h"
#include "Types.h"

struct VendTimings
{
    // Cloud response time limits. They must be larger than the request TTL + the worst-case run time
    // of one operation in the background (TLS handshake + send + read).
    uint32_t fetchUserTimeoutMs = 30000;
    uint32_t updateUserTimeoutMs = 30000;
    uint32_t configTimeoutMs = 30000;
    // How long a request may wait in the queue; after that the worker does not execute it any more
    uint32_t requestTtlMs = 5000;

    // After the pulses the machine must raise inhibit HIGH within this time (credit accepted)
    uint32_t vendAckTimeoutMs = 2000;
    // Maximum drink preparation time (inhibit HIGH), after that a "machine stuck" error
    uint32_t vendCompleteTimeoutMs = 180000;

    uint32_t successScreenMs = 3000;
    uint32_t errorScreenMs = 4000;
    uint32_t cardHoldOffMs = 2000;
    // In free mode no new credit is given for this long after the previous drink finished
    uint32_t freeRetriggerDelayMs = 1000;

    // Full config fetch only at startup and after a WiFi reconnect; retried after this time on failure.
    // Changes in between arrive through the stream (ConfigChanged).
    uint32_t configRetryMs = 10000;
    uint32_t statusRefreshMs = 250;

    // Remote purchase (app): a request older than this (server timestamp vs NTP time) is refused
    uint32_t remoteMaxAgeMs = 20000;
    // tolerated clock difference for requests that look like they come from the future
    uint32_t remoteMaxClockSkewMs = 5000;
    // availability heartbeat (remote/<dev>/online) interval, and the minimum gap between two writes
    // when the availability changes in between
    uint32_t remoteHeartbeatMs = 60000;
    uint32_t remoteHeartbeatMinGapMs = 2000;
    // how long a status write may wait in the queue
    uint32_t remoteStatusTtlMs = 60000;
};

// Non-blocking state machine of the purchase flow.
//
//   NotReady ──(WiFi+time+Firebase+config+reader OK)──► Idle
//   Idle ──card──► Authorizing ──has credit──► Charging ──charged──► Pulsing ──► WaitVendAck ──► Vending ──► ShowResult ──► Idle
//                      │                           │                                  │              │
//                      └─ unknown / no credit / error ───────────────────────────────► ShowResult (error screen)
//   Idle (free mode) ──inhibit LOW──► Pulsing ──► WaitVendAck ──► Vending ──► Idle
//   Idle ──app request (RemoteVend)──► Authorizing ──► (same as a card)
//
// A remote ("virtual card tap") request is only taken in Idle (or ShowResult, which it cuts short), in normal
// mode, with inhibit LOW and if it is younger than remoteMaxAgeMs; otherwise it is refused with E33 without
// touching the state. A card and a request in the same loop: the card wins. Progress is reported to the app
// through RemoteStatus requests (accepted -> charged -> done | error).
//
// The order follows the original behaviour: the charge and the log happen BEFORE the credit pulses.
// Differences from the original (stability): no pulses if the charge failed; every wait has a timeout.
class VendController
{
public:
    enum class State : uint8_t
    {
        NotReady,
        Idle,
        Authorizing,
        Charging,
        Pulsing,
        WaitVendAck,
        Vending,
        ShowResult,
    };

    VendController(ICardReader &reader, ICoinMechanism &coin, IUi &ui, IFeedback &feedback,
                   ICloud &cloud, INetwork &net, IClock &clock, const VendTimings &timings = VendTimings());

    // cachedConfig: the last saved (e.g. NVS) configuration, used until a fresh one arrives from the cloud
    // remoteMark: the last handled app purchase request (saved), so a reboot cannot run it again
    void begin(const DeviceConfig &cachedConfig, const RemoteMark &remoteMark = RemoteMark());
    void loop();

    State state() const { return state_; }
    const DeviceConfig &config() const { return config_; }
    // true if the configuration changed since the last call (for saving)
    bool consumeConfigChanged();
    // true if a new app request was handled since the last call - save remoteMark() (before the charge
    // can start: the charge is submitted in a later loop() at the earliest)
    bool consumeRemoteMarkChanged();
    const RemoteMark &remoteMark() const { return remoteMark_; }
    // Redraws the screen of the current state (if something else was shown meanwhile, e.g. OTA)
    void refreshScreen();

    static const char *stateName(State s);

private:
    void setState(State s, uint32_t nowMs);

    ErrorInfo readinessError() const;
    void handleNotReady(uint32_t nowMs);
    void handleIdle(uint32_t nowMs, bool hasCard, const CardUid &uid);
    void handleCard(uint32_t nowMs, const CardUid &uid);

    void processCloudResponses(uint32_t nowMs, bool cardPending);
    void onUserFetched(uint32_t nowMs, const CloudResponse &resp);
    void onUserUpdated(uint32_t nowMs, const CloudResponse &resp);
    void onConfig(uint32_t nowMs, const CloudResponse &resp);
    void onConfigPush(const CloudResponse &resp);
    // offers a new configuration: applied immediately when idle, deferred during a transaction
    void offerConfig(const DeviceConfig &candidate, const char *source);
    void applyPendingConfig();
    bool configChangeAllowed() const { return state_ == State::NotReady || state_ == State::Idle; }
    void scheduleConfigRefresh(uint32_t nowMs);
    void checkTimeouts(uint32_t nowMs);

    void onRemoteRequest(uint32_t nowMs, const CloudResponse &resp, bool cardPending);
    void rejectRemote(uint32_t nowMs, const char *reqId, ErrorCode code, const char *why);
    void sendRemoteStatus(uint32_t nowMs, const char *reqId, RemoteState st, bool deleteRequest, ErrorCode code = ErrorCode::None);
    void finishRemote(uint32_t nowMs, RemoteState st, ErrorCode code = ErrorCode::None);
    bool remoteReady() const;
    void updateRemotePresence(uint32_t nowMs);

    void startVend(uint32_t nowMs, bool freeVend);
    void handleVendProgress(uint32_t nowMs);

    bool submit(CloudRequest &req, uint32_t nowMs);
    void submitLog(uint32_t nowMs, const char *uid, LogAction action, int32_t credit);
    // device status log (/Log)
    void logEvent(uint32_t nowMs, const char *event, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;
    void trackReaderHealth(uint32_t nowMs);

    void showIdle();
    void showNotReady(const ErrorInfo &e);
    void showChecking();
    void showSuccess(const char *text);
    void showFreeVend();
    void showError(uint32_t nowMs, const ErrorInfo &e);
    void showResultFor(uint32_t nowMs, uint32_t durationMs);

    void updateStatus(uint32_t nowMs);

    ICardReader &reader_;
    ICoinMechanism &coin_;
    IUi &ui_;
    IFeedback &feedback_;
    ICloud &cloud_;
    INetwork &net_;
    IClock &clock_;
    VendTimings timings_;

    State state_ = State::NotReady;
    uint32_t stateSinceMs_ = 0;
    uint32_t deadlineMs_ = 0;
    uint32_t resultUntilMs_ = 0;

    DeviceConfig config_;
    bool configChanged_ = false;
    uint32_t configRequestId_ = 0;
    uint32_t configRequestDeadlineMs_ = 0;
    uint32_t nextConfigFetchMs_ = 0;
    uint32_t fetchedConnectionId_ = 0;  // connection for which the full config was last loaded
    uint32_t requestedConnectionId_ = 0;
    DeviceConfig pendingConfig_;
    bool hasPendingConfig_ = false;
    uint32_t pendingEventNowMs_ = 0;

    // reader status log (throttled so a flapping fault does not flood the log)
    bool readerHealthy_ = true;
    bool readerStateKnown_ = false;
    uint32_t lastReaderEventMs_ = 0;
    uint32_t readerFlapsSuppressed_ = 0;
    const char *pendingSource_ = "";

    uint32_t nextRequestId_ = 1;
    uint32_t pendingId_ = 0;

    UserRecord user_;
    int32_t chargedPrice_ = 0;
    int32_t newCredit_ = 0;
    bool freeVend_ = false;
    bool charged_ = false; // the account of the current transaction was charged

    // remote purchase
    bool remote_ = false; // the current transaction came from the app
    char remoteId_[kRemoteIdSize] = {0};
    RemoteMark remoteMark_;
    bool remoteMarkChanged_ = false;
    bool presenceAttempted_ = false;
    bool presenceReady_ = false;
    uint32_t lastPresenceMs_ = 0;
    uint32_t freeRetriggerAtMs_ = 0;
    uint32_t successShownAtMs_ = 0;

    CardDebouncer debouncer_;
    bool idleShownFreeMode_ = false;
    bool idleShown_ = false;
    ErrorInfo shownNotReady_;
    uint32_t lastStatusMs_ = 0;
    bool statusSent_ = false;
};
