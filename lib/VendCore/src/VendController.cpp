#include "VendController.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Log.h"
#include "Messages.h"
#include "PurchasePolicy.h"

namespace
{
    constexpr const char *TAG = "VEND";
    constexpr const char *kFreeModeUid = "0";                       // the original system also logged free purchases with "0"
    constexpr uint32_t kLogTtlMs = 10UL * 60UL * 1000UL;            // a log may wait up to 10 minutes while offline
    constexpr uint32_t kReaderEventMinIntervalMs = 5UL * 60UL * 1000UL; // min. interval of reader events in /Log

    // millis() overflow-safe comparison
    inline bool reached(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

    inline void copyStr(char *dst, size_t dstSize, const char *src)
    {
        if (dstSize == 0)
            return;
        snprintf(dst, dstSize, "%s", src != nullptr ? src : "");
    }

    inline bool sameError(const ErrorInfo &a, const ErrorInfo &b)
    {
        return a.code == b.code && a.detailCode == b.detailCode && strcmp(a.detail, b.detail) == 0;
    }
} // namespace

VendController::VendController(ICardReader &reader, ICoinMechanism &coin, IUi &ui, IFeedback &feedback,
                               ICloud &cloud, INetwork &net, IClock &clock, const VendTimings &timings)
    : reader_(reader), coin_(coin), ui_(ui), feedback_(feedback), cloud_(cloud), net_(net), clock_(clock),
      timings_(timings), debouncer_(timings.cardHoldOffMs)
{
}

const char *VendController::stateName(State s)
{
    switch (s)
    {
    case State::NotReady: return "NotReady";
    case State::Idle: return "Idle";
    case State::Authorizing: return "Authorizing";
    case State::Charging: return "Charging";
    case State::Pulsing: return "Pulsing";
    case State::WaitVendAck: return "WaitVendAck";
    case State::Vending: return "Vending";
    case State::ShowResult: return "ShowResult";
    }
    return "?";
}

void VendController::begin(const DeviceConfig &cachedConfig)
{
    config_ = cachedConfig;
    if (config_.loaded)
        LOGI(TAG, "Saved configuration: price=%ld Ft, free=%s", (long)config_.price, config_.freeMode ? "yes" : "no");

    const uint32_t now = clock_.nowMs();
    nextConfigFetchMs_ = now;
    setState(State::NotReady, now);
    shownNotReady_ = ErrorInfo::none();
    showNotReady(ErrorInfo::make(ErrorCode::None, 0, "%s", msg::kBootTitle));
}

bool VendController::consumeConfigChanged()
{
    const bool changed = configChanged_;
    configChanged_ = false;
    return changed;
}

void VendController::refreshScreen()
{
    idleShown_ = false;
    shownNotReady_ = ErrorInfo::none();
    shownNotReady_.code = static_cast<ErrorCode>(0xFFFF); // guaranteed to differ -> redraw
}

void VendController::setState(State s, uint32_t nowMs)
{
    if (s != state_)
        LOGD(TAG, "State: %s -> %s", stateName(state_), stateName(s));
    state_ = s;
    stateSinceMs_ = nowMs;
    if (s != State::Idle)
        idleShown_ = false;
}

// ------------------------------------------------------------------ main loop

void VendController::loop()
{
    const uint32_t now = clock_.nowMs();

    CardUid uid;
    bool hasCard = false;
    if (reader_.poll(uid))
    {
        hasCard = debouncer_.accept(uid, now);
        if (hasCard && state_ != State::Idle)
        {
            char hex[CardUid::kHexBufferSize];
            uid.toHex(hex, sizeof(hex));
            LOGI(TAG, "Card (%s) ignored, state: %s", hex, stateName(state_));
            hasCard = false;
        }
    }

    pendingEventNowMs_ = now; // for the event log of offerConfig()
    processCloudResponses(now);
    trackReaderHealth(now);
    applyPendingConfig();
    scheduleConfigRefresh(now);
    checkTimeouts(now);

    switch (state_)
    {
    case State::NotReady:
        handleNotReady(now);
        break;
    case State::Idle:
        handleIdle(now, hasCard, uid);
        break;
    case State::Authorizing:
    case State::Charging:
        // waiting for the cloud response (processCloudResponses / checkTimeouts)
        break;
    case State::Pulsing:
    case State::WaitVendAck:
    case State::Vending:
        handleVendProgress(now);
        break;
    case State::ShowResult:
        if (reached(now, resultUntilMs_))
        {
            const ErrorInfo notReady = readinessError();
            if (notReady.isError())
                setState(State::NotReady, now);
            else
                setState(State::Idle, now);
        }
        break;
    }

    updateStatus(now);
}

// ------------------------------------------------------------------ readiness

ErrorInfo VendController::readinessError() const
{
    if (!net_.connected())
    {
        char detail[80];
        net_.statusDetail(detail, sizeof(detail));
        return ErrorInfo::make(ErrorCode::WifiDisconnected, 0, "%s", detail[0] ? detail : "Connecting to the configured networks...");
    }
    if (!clock_.synced())
        return ErrorInfo::make(ErrorCode::TimeNotSynced, 0, "Waiting for the NTP server...");
    if (!cloud_.ready())
    {
        const ErrorInfo e = cloud_.statusError();
        if (e.isError())
            return e;
        return ErrorInfo::make(ErrorCode::CloudNotReady, 0, "Firebase sign-in in progress...");
    }
    if (!config_.loaded)
        return ErrorInfo::make(ErrorCode::CloudNotReady, 0, "Loading configuration (price, mode)...");
    if (!reader_.healthy())
    {
        ErrorInfo e = reader_.lastError();
        if (!e.isError())
            e = ErrorInfo::make(ErrorCode::ReaderFault, 0, "%s not responding", reader_.name());
        return e;
    }
    if (!config_.freeMode && !config_.priceValid())
        return ErrorInfo::make(ErrorCode::ConfigInvalid, config_.price,
                               "Invalid price (conf/price): %ld Ft", (long)config_.price);
    return ErrorInfo::none();
}

void VendController::handleNotReady(uint32_t nowMs)
{
    const ErrorInfo e = readinessError();
    if (!e.isError())
    {
        LOGI(TAG, "System ready (mode: %s, price: %ld Ft)", config_.freeMode ? "free" : "normal", (long)config_.price);
        setState(State::Idle, nowMs);
        return;
    }
    if (!sameError(e, shownNotReady_))
    {
        LOGW(TAG, "Not ready: E%u %s - %s", static_cast<unsigned>(e.code), errorCodeName(e.code), e.detail);
        showNotReady(e);
    }
}

// ------------------------------------------------------------------ Idle

void VendController::handleIdle(uint32_t nowMs, bool hasCard, const CardUid &uid)
{
    const ErrorInfo notReady = readinessError();
    if (notReady.isError())
    {
        setState(State::NotReady, nowMs);
        shownNotReady_ = ErrorInfo::none();
        handleNotReady(nowMs);
        return;
    }

    if (!idleShown_ || idleShownFreeMode_ != config_.freeMode)
        showIdle();

    if (config_.freeMode)
    {
        // Same as the original: no card reading in free mode - if the machine waits for credit, it gets it.
        if (coin_.acceptingCredit() && reached(nowMs, freeRetriggerAtMs_))
        {
            LOGI(TAG, "Free mode: sending credit");
            startVend(nowMs, true);
        }
        return;
    }

    if (hasCard)
        handleCard(nowMs, uid);
}

void VendController::handleCard(uint32_t nowMs, const CardUid &uid)
{
    feedback_.play(FeedbackPattern::CardAccepted);

    user_ = UserRecord();
    uid.toHex(user_.uid, sizeof(user_.uid));
    LOGI(TAG, "Card: %s", user_.uid);

    if (!coin_.acceptingCredit())
    {
        showError(nowMs, ErrorInfo::make(ErrorCode::MachineNotReady, 0,
                                         "Inhibit active (machine busy, faulty or already has credit). UID: %s",
                                         user_.uid));
        return;
    }

    CloudRequest req;
    req.op = CloudOp::FetchUser;
    copyStr(req.uid, sizeof(req.uid), user_.uid);
    if (!submit(req, nowMs))
        return;

    showChecking();
    deadlineMs_ = nowMs + timings_.fetchUserTimeoutMs;
    setState(State::Authorizing, nowMs);
}

// ------------------------------------------------------------------ cloud

bool VendController::submit(CloudRequest &req, uint32_t nowMs)
{
    req.id = nextRequestId_++;
    if (nextRequestId_ == 0)
        nextRequestId_ = 1;
    req.deadlineMs = nowMs + timings_.requestTtlMs;
    if (!cloud_.submit(req))
    {
        showError(nowMs, ErrorInfo::make(ErrorCode::CloudRequest, 0, "Cloud request queue full (op=%u)",
                                         static_cast<unsigned>(req.op)));
        return false;
    }
    if (req.op == CloudOp::FetchUser || req.op == CloudOp::UpdateUser)
        pendingId_ = req.id;
    return true;
}

void VendController::submitLog(uint32_t nowMs, const char *uid, LogAction action, int32_t credit)
{
    CloudRequest req;
    req.op = CloudOp::WriteLog;
    req.id = nextRequestId_++;
    if (nextRequestId_ == 0)
        nextRequestId_ = 1;
    // the log is not time-critical, the worker may retry it
    req.deadlineMs = nowMs + kLogTtlMs;
    copyStr(req.uid, sizeof(req.uid), uid);
    req.action = action;
    req.credit = credit;
    req.timestamp = clock_.epoch();
    if (!cloud_.submit(req))
        LOGE(TAG, "Log NOT queued (queue full): uid=%s action=%ld credit=%ld", uid, (long)action, (long)credit);
}

void VendController::logEvent(uint32_t nowMs, const char *event, const char *fmt, ...)
{
    CloudRequest req;
    req.op = CloudOp::DeviceEvent;
    req.id = nextRequestId_++;
    if (nextRequestId_ == 0)
        nextRequestId_ = 1;
    req.deadlineMs = nowMs + kLogTtlMs;
    req.timestamp = clock_.synced() ? clock_.epoch() : 0;
    snprintf(req.event, sizeof(req.event), "%s", event);
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.detail, sizeof(req.detail), fmt, args);
    va_end(args);
    if (!cloud_.submit(req))
        LOGE(TAG, "Device event NOT queued: %s: %s", req.event, req.detail);
}

void VendController::trackReaderHealth(uint32_t nowMs)
{
    const bool healthy = reader_.healthy();
    if (!readerStateKnown_)
    {
        readerStateKnown_ = true;
        readerHealthy_ = healthy;
        if (!healthy)
        {
            lastReaderEventMs_ = nowMs;
            logEvent(nowMs, "reader", "%s fault at startup: %s", reader_.name(), reader_.lastError().detail);
        }
        return;
    }
    if (healthy == readerHealthy_)
        return;
    readerHealthy_ = healthy;

    // throttling: a flapping reader must not write every minute
    if (lastReaderEventMs_ != 0 && !reached(nowMs, lastReaderEventMs_ + kReaderEventMinIntervalMs))
    {
        readerFlapsSuppressed_++;
        return;
    }
    lastReaderEventMs_ = nowMs;
    if (healthy)
        logEvent(nowMs, "reader", "%s recovered (%lu state changes suppressed)", reader_.name(),
                 (unsigned long)readerFlapsSuppressed_);
    else
        logEvent(nowMs, "reader", "%s fault: %s (%lu state changes suppressed)", reader_.name(), reader_.lastError().detail,
                 (unsigned long)readerFlapsSuppressed_);
    readerFlapsSuppressed_ = 0;
}

void VendController::processCloudResponses(uint32_t nowMs)
{
    CloudResponse resp;
    while (cloud_.poll(resp))
    {
        if (resp.op == CloudOp::FetchConfig)
        {
            onConfig(nowMs, resp);
            continue;
        }
        if (resp.op == CloudOp::ConfigChanged)
        {
            onConfigPush(resp);
            continue;
        }
        if (resp.op == CloudOp::WriteLog || resp.op == CloudOp::DeviceEvent)
            continue; // the worker logs the result itself

        if (resp.id != pendingId_ || pendingId_ == 0)
        {
            if (resp.op == CloudOp::UpdateUser && resp.ok)
                LOGE(TAG, "LATE response: credit of %s updated (%ld Ft), but the transaction already timed out - credit was NOT sent!",
                     resp.user.uid, (long)resp.user.credit);
            else
                LOGW(TAG, "Stale cloud response ignored (id=%lu, op=%u)", (unsigned long)resp.id,
                     static_cast<unsigned>(resp.op));
            continue;
        }
        pendingId_ = 0;

        if (state_ == State::Authorizing && resp.op == CloudOp::FetchUser)
            onUserFetched(nowMs, resp);
        else if (state_ == State::Charging && resp.op == CloudOp::UpdateUser)
            onUserUpdated(nowMs, resp);
        else
            LOGW(TAG, "Unexpected response (op=%u) in state: %s", static_cast<unsigned>(resp.op), stateName(state_));
    }
}

void VendController::onUserFetched(uint32_t nowMs, const CloudResponse &resp)
{
    if (!resp.ok)
    {
        if (resp.notFound)
            showError(nowMs, ErrorInfo::make(ErrorCode::CardUnknown, 0, "UID: %s", user_.uid));
        else
            showError(nowMs, resp.error);
        return;
    }

    user_ = resp.user;
    const int32_t price = config_.price;
    LOGI(TAG, "User %s (%s): credit %ld Ft, price %ld Ft", user_.uid, user_.name[0] ? user_.name : "-",
         (long)user_.credit, (long)price);

    if (!PurchasePolicy::canPurchase(user_.credit, price))
    {
        ErrorInfo e = ErrorInfo::make(ErrorCode::CreditLow, user_.credit, "Credit: %ld Ft, price: %ld Ft",
                                      (long)user_.credit, (long)price);
        showError(nowMs, e);
        return;
    }

    chargedPrice_ = price;
    newCredit_ = PurchasePolicy::creditAfterPurchase(user_.credit, price);

    CloudRequest req;
    req.op = CloudOp::UpdateUser;
    copyStr(req.uid, sizeof(req.uid), user_.uid);
    req.credit = newCredit_;
    req.timestamp = clock_.epoch();
    if (!submit(req, nowMs))
        return;

    deadlineMs_ = nowMs + timings_.updateUserTimeoutMs;
    setState(State::Charging, nowMs);
}

void VendController::onUserUpdated(uint32_t nowMs, const CloudResponse &resp)
{
    if (!resp.ok)
    {
        ErrorInfo e = ErrorInfo::make(ErrorCode::CreditUpdateFailed, resp.error.detailCode, "E%u: %s",
                                      static_cast<unsigned>(resp.error.code), resp.error.detail);
        showError(nowMs, e);
        return;
    }

    LOGI(TAG, "Charged %ld Ft, new credit: %ld Ft (%s)", (long)chargedPrice_, (long)newCredit_, user_.uid);
    user_.credit = newCredit_; // the log entry was written together with the credit (users/<UID>/log)

    // Same as the original: pulses only if the machine still accepts credit
    if (!coin_.acceptingCredit())
    {
        showError(nowMs, ErrorInfo::make(ErrorCode::ChargedNotVended, chargedPrice_,
                                         "%s: charged %ld Ft (new credit %ld Ft), but the machine raised inhibit meanwhile",
                                         user_.uid, (long)chargedPrice_, (long)newCredit_));
        return;
    }
    startVend(nowMs, false);
}

void VendController::onConfig(uint32_t nowMs, const CloudResponse &resp)
{
    if (resp.id != configRequestId_)
        return;
    configRequestId_ = 0;

    if (!resp.ok)
    {
        LOGW(TAG, "Config fetch failed (E%u: %s) - keeping: price=%ld, free=%d, retry in %lu s",
             static_cast<unsigned>(resp.error.code), resp.error.detail, (long)config_.price, config_.freeMode,
             (unsigned long)(timings_.configRetryMs / 1000));
        nextConfigFetchMs_ = nowMs + timings_.configRetryMs;
        return;
    }

    fetchedConnectionId_ = requestedConnectionId_;
    DeviceConfig fresh = resp.config;
    fresh.loaded = true;
    offerConfig(fresh, "fetch");
}

void VendController::onConfigPush(const CloudResponse &resp)
{
    // the change builds on the latest (possibly still deferred) state
    DeviceConfig candidate = hasPendingConfig_ ? pendingConfig_ : config_;
    if (resp.hasPrice)
    {
        candidate.price = resp.config.price;
        candidate.loaded = true;
    }
    if (resp.hasFree)
        candidate.freeMode = resp.config.freeMode;
    if (!resp.hasPrice && !resp.hasFree)
        return;
    offerConfig(candidate, "stream");
}

void VendController::offerConfig(const DeviceConfig &candidate, const char *source)
{
    if (!configChangeAllowed())
    {
        if (candidate != (hasPendingConfig_ ? pendingConfig_ : config_))
            LOGI(TAG, "Config change (%s) deferred until the end of the transaction", source);
        pendingConfig_ = candidate;
        pendingSource_ = source;
        hasPendingConfig_ = true;
        return;
    }
    hasPendingConfig_ = false;
    if (candidate == config_)
        return;

    LOGI(TAG, "Config (%s): price %ld -> %ld Ft, free %d -> %d", source, (long)config_.price, (long)candidate.price,
         config_.freeMode, candidate.freeMode);
    if (!config_.loaded)
    {
        logEvent(pendingEventNowMs_, "config", "loaded (%s): price %ld Ft, mode: %s", source, (long)candidate.price,
                 candidate.freeMode ? "free" : "normal");
    }
    else
    {
        if (candidate.price != config_.price)
            logEvent(pendingEventNowMs_, "config", "price: %ld -> %ld Ft (%s)", (long)config_.price, (long)candidate.price, source);
        if (candidate.freeMode != config_.freeMode)
            logEvent(pendingEventNowMs_, "config", "mode: %s -> %s (%s)", config_.freeMode ? "free" : "normal",
                     candidate.freeMode ? "free" : "normal", source);
    }
    config_ = candidate;
    configChanged_ = true;
    idleShown_ = false; // the Idle screen shows the price / mode
}

void VendController::applyPendingConfig()
{
    if (hasPendingConfig_ && configChangeAllowed())
        offerConfig(pendingConfig_, pendingSource_);
}

void VendController::scheduleConfigRefresh(uint32_t nowMs)
{
    // Full fetch only at startup and after a reconnect (new connectionId); everything else comes via the stream.
    if (configRequestId_ != 0 || !configChangeAllowed())
        return;
    if (!net_.connected() || !cloud_.ready())
        return;
    const uint32_t connId = net_.connectionId();
    if (connId == fetchedConnectionId_ || !reached(nowMs, nextConfigFetchMs_))
        return;

    CloudRequest req;
    req.op = CloudOp::FetchConfig;
    req.id = nextRequestId_++;
    if (nextRequestId_ == 0)
        nextRequestId_ = 1;
    req.deadlineMs = nowMs + timings_.requestTtlMs;
    if (cloud_.submit(req))
    {
        LOGI(TAG, "Fetching full configuration (connection #%lu)", (unsigned long)connId);
        configRequestId_ = req.id;
        requestedConnectionId_ = connId;
        configRequestDeadlineMs_ = nowMs + timings_.configTimeoutMs;
    }
    else
    {
        nextConfigFetchMs_ = nowMs + timings_.configRetryMs;
    }
}

void VendController::checkTimeouts(uint32_t nowMs)
{
    if (configRequestId_ != 0 && reached(nowMs, configRequestDeadlineMs_))
    {
        LOGW(TAG, "Config fetch timed out");
        configRequestId_ = 0;
        nextConfigFetchMs_ = nowMs + timings_.configRetryMs;
    }

    if (state_ == State::Authorizing && reached(nowMs, deadlineMs_))
    {
        pendingId_ = 0;
        showError(nowMs, ErrorInfo::make(ErrorCode::CloudTimeout, 0, "Fetch user (%s): no response within %lu s",
                                         user_.uid, (unsigned long)(timings_.fetchUserTimeoutMs / 1000)));
    }
    else if (state_ == State::Charging && reached(nowMs, deadlineMs_))
    {
        pendingId_ = 0;
        LOGE(TAG, "Credit update timed out (%s) - the charge state is uncertain!", user_.uid);
        showError(nowMs, ErrorInfo::make(ErrorCode::CloudTimeout, 0,
                                         "Update credit (%s): no response. Charge uncertain, credit was not sent",
                                         user_.uid));
    }
}

// ------------------------------------------------------------------ credit hand-over

void VendController::startVend(uint32_t nowMs, bool freeVend)
{
    freeVend_ = freeVend;
    if (!coin_.startCreditPulses())
    {
        showError(nowMs, ErrorInfo::make(ErrorCode::VendNotAcknowledged, 0, "Could not start the pulses (already running?)"));
        return;
    }
    if (freeVend)
    {
        showFreeVend();
    }
    else
    {
        showSuccess(msg::kVendInProgress);
        successShownAtMs_ = nowMs;
    }
    setState(State::Pulsing, nowMs);
}

void VendController::handleVendProgress(uint32_t nowMs)
{
    switch (state_)
    {
    case State::Pulsing:
        if (!coin_.pulsing())
        {
            deadlineMs_ = nowMs + timings_.vendAckTimeoutMs;
            setState(State::WaitVendAck, nowMs);
        }
        break;

    case State::WaitVendAck:
        if (!coin_.acceptingCredit())
        {
            LOGI(TAG, "Machine accepted the credit (inhibit HIGH)");
            deadlineMs_ = nowMs + timings_.vendCompleteTimeoutMs;
            setState(State::Vending, nowMs);
        }
        else if (reached(nowMs, deadlineMs_))
        {
            // The original code silently continued here; now it is reported, but the charge is not reverted.
            if (freeVend_)
            {
                freeRetriggerAtMs_ = nowMs + timings_.errorScreenMs;
                showError(nowMs, ErrorInfo::make(ErrorCode::VendNotAcknowledged, 0,
                                                 "Free credit: inhibit did not go HIGH within %lu ms",
                                                 (unsigned long)timings_.vendAckTimeoutMs));
            }
            else
            {
                showError(nowMs, ErrorInfo::make(ErrorCode::VendNotAcknowledged, chargedPrice_,
                                                 "%s: charged %ld Ft, but inhibit did not go HIGH within %lu ms",
                                                 user_.uid, (long)chargedPrice_, (unsigned long)timings_.vendAckTimeoutMs));
            }
        }
        break;

    case State::Vending:
        if (coin_.acceptingCredit())
        {
            LOGI(TAG, "Drink finished (inhibit LOW, %lu ms)", (unsigned long)(nowMs - stateSinceMs_));
            if (freeVend_)
            {
                submitLog(nowMs, kFreeModeUid, LogAction::FreePurchase, 0);
                freeRetriggerAtMs_ = nowMs + timings_.freeRetriggerDelayMs;
                setState(State::Idle, nowMs);
            }
            else
            {
                showSuccess(msg::kVendDone);
                const uint32_t minUntil = successShownAtMs_ + timings_.successScreenMs;
                showResultFor(nowMs, reached(nowMs, minUntil) ? 1000 : (minUntil - nowMs));
            }
        }
        else if (reached(nowMs, deadlineMs_))
        {
            if (freeVend_)
                submitLog(nowMs, kFreeModeUid, LogAction::FreePurchase, 0);
            freeRetriggerAtMs_ = nowMs + timings_.freeRetriggerDelayMs;
            showError(nowMs, ErrorInfo::make(ErrorCode::MachineStuck, 0,
                                             "Inhibit still HIGH after %lu s (machine fault or stuck credit?)",
                                             (unsigned long)(timings_.vendCompleteTimeoutMs / 1000)));
        }
        break;

    default:
        break;
    }
}

// ------------------------------------------------------------------ screens

void VendController::showIdle()
{
    Screen s;
    if (config_.freeMode)
    {
        s.kind = ScreenKind::FreeIdle;
        copyStr(s.title, sizeof(s.title), msg::kFreeTitle);
        copyStr(s.text, sizeof(s.text), msg::kFreeHint);
    }
    else
    {
        s.kind = ScreenKind::Idle;
        copyStr(s.title, sizeof(s.title), msg::kPayPrompt);
        copyStr(s.text, sizeof(s.text), msg::kPayHint);
        s.hasAmount = true;
        s.amount = config_.price;
    }
    ui_.show(s);
    idleShown_ = true;
    idleShownFreeMode_ = config_.freeMode;
    LOGI(TAG, "%s", config_.freeMode ? "Free mode, waiting for the machine..." : "Waiting for a card...");
}

void VendController::showNotReady(const ErrorInfo &e)
{
    shownNotReady_ = e;
    Screen s;
    s.kind = ScreenKind::Boot;
    s.error = e;
    // title + text are Hungarian (display language); the English technical detail travels in s.error
    if (e.isError())
    {
        copyStr(s.title, sizeof(s.title), msg::errorTitle(e.code));
        copyStr(s.text, sizeof(s.text), msg::errorHint(e.code));
    }
    else
    {
        copyStr(s.title, sizeof(s.title), msg::kBootTitle);
    }
    ui_.show(s);
}

void VendController::showChecking()
{
    Screen s;
    s.kind = ScreenKind::Checking;
    copyStr(s.title, sizeof(s.title), msg::kChecking);
    snprintf(s.text, sizeof(s.text), "%s: %s", msg::kCardRead, user_.uid);
    ui_.show(s);
}

void VendController::showSuccess(const char *text)
{
    Screen s;
    s.kind = ScreenKind::Success;
    copyStr(s.title, sizeof(s.title), user_.name[0] ? user_.name : msg::kBalance);
    copyStr(s.text, sizeof(s.text), text);
    s.hasAmount = true;
    s.amount = user_.credit;
    ui_.show(s);
}

void VendController::showFreeVend()
{
    Screen s;
    s.kind = ScreenKind::FreeVend;
    copyStr(s.title, sizeof(s.title), msg::kFreeTitle);
    copyStr(s.text, sizeof(s.text), msg::kFreeVend);
    ui_.show(s);
}

void VendController::showError(uint32_t nowMs, const ErrorInfo &e)
{
    LOGE(TAG, "E%u %s | %s (detail code: %ld)", static_cast<unsigned>(e.code), errorCodeName(e.code), e.detail,
         (long)e.detailCode);
    // errors affecting money or the machine also go to the device log
    const bool important = e.code == ErrorCode::CreditUpdateFailed || e.code == ErrorCode::VendNotAcknowledged ||
                           e.code == ErrorCode::MachineStuck || e.code == ErrorCode::ChargedNotVended ||
                           (e.code == ErrorCode::CloudTimeout && state_ == State::Charging);
    if (important)
        logEvent(nowMs, "error", "E%u %s: %s", static_cast<unsigned>(e.code), errorCodeName(e.code), e.detail);
    Screen s;
    s.kind = ScreenKind::Error;
    s.error = e;
    copyStr(s.title, sizeof(s.title), msg::errorTitle(e.code));
    copyStr(s.text, sizeof(s.text), msg::errorHint(e.code));
    if (e.code == ErrorCode::CreditLow)
    {
        s.hasAmount = true;
        s.amount = e.detailCode;
    }
    ui_.show(s);
    feedback_.play(FeedbackPattern::Error);
    showResultFor(nowMs, timings_.errorScreenMs);
}

void VendController::showResultFor(uint32_t nowMs, uint32_t durationMs)
{
    resultUntilMs_ = nowMs + durationMs;
    setState(State::ShowResult, nowMs);
}

// ------------------------------------------------------------------ status bar

void VendController::updateStatus(uint32_t nowMs)
{
    if (statusSent_ && !reached(nowMs, lastStatusMs_ + timings_.statusRefreshMs))
        return;
    lastStatusMs_ = nowMs;
    statusSent_ = true;

    StatusInfo st;
    st.wifi = net_.connected();
    if (st.wifi)
    {
        net_.ssid(st.ssid, sizeof(st.ssid));
        net_.ip(st.ip, sizeof(st.ip));
        st.rssi = net_.rssi();
    }
    st.cloud = cloud_.ready();
    st.timeSynced = clock_.synced();
    if (st.timeSynced)
        clock_.formatLocalTime(st.clock, sizeof(st.clock));
    st.machineReady = coin_.acceptingCredit();
    st.freeMode = config_.freeMode;
    st.price = config_.price;
    st.liveConfig = cloud_.liveUpdates();
    ui_.setStatus(st);
}
