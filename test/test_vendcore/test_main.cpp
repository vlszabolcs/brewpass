// Native unit tests of the hardware independent VendCore logic:  pio test -e native
#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "CardDebouncer.h"
#include "Messages.h"
#include "PurchasePolicy.h"
#include "VendController.h"

// ------------------------------------------------------------------ fakes

struct FakeClock : IClock
{
    uint32_t now = 1000;
    bool isSynced = true;
    uint32_t nowMs() const override { return now; }
    bool synced() const override { return isSynced; }
    uint32_t epoch() const override { return 1760000000u + now / 1000; }
    void formatLocalTime(char *out, size_t n) const override { snprintf(out, n, "12:00:00"); }
};

struct FakeNet : INetwork
{
    bool up = true;
    uint32_t connId = 1;
    uint32_t connectionId() const override { return connId; }
    bool connected() const override { return up; }
    void ssid(char *out, size_t n) const override { snprintf(out, n, "test"); }
    void ip(char *out, size_t n) const override { snprintf(out, n, "192.168.1.10"); }
    int8_t rssi() const override { return -50; }
    void statusDetail(char *out, size_t n) const override { snprintf(out, n, "%s", ""); }
};

struct FakeReader : ICardReader
{
    std::deque<CardUid> cards;
    bool ok = true;
    ErrorInfo err;
    bool begin() override { return true; }
    bool poll(CardUid &out) override
    {
        if (cards.empty())
            return false;
        out = cards.front();
        cards.pop_front();
        return true;
    }
    bool healthy() const override { return ok; }
    const ErrorInfo &lastError() const override { return err; }
    const char *name() const override { return "fake"; }
};

struct FakeCoin : ICoinMechanism
{
    bool accepting = true; // inhibit LOW
    int pulseTrains = 0;
    int pulseLoopsLeft = 0;
    void begin() override {}
    void loop(uint32_t) override
    {
        if (pulseLoopsLeft > 0)
            pulseLoopsLeft--;
    }
    bool acceptingCredit() const override { return accepting; }
    bool startCreditPulses() override
    {
        if (pulseLoopsLeft > 0)
            return false;
        pulseTrains++;
        pulseLoopsLeft = 2;
        return true;
    }
    bool pulsing() const override { return pulseLoopsLeft > 0; }
    const char *name() const override { return "fake"; }
};

struct FakeUi : IUi
{
    std::vector<Screen> screens;
    bool begin() override { return true; }
    void show(const Screen &s) override { screens.push_back(s); }
    void setStatus(const StatusInfo &) override {}
    void loop(uint32_t) override {}
    const Screen &last() const { return screens.back(); }
};

struct FakeFeedback : IFeedback
{
    std::vector<FeedbackPattern> played;
    void begin() override {}
    void play(FeedbackPattern p) override { played.push_back(p); }
    void loop(uint32_t) override {}
};

struct FakeCloud : ICloud
{
    bool isReady = true;
    bool isRemoteLive = true;
    std::vector<CloudRequest> requests;
    std::deque<CloudResponse> responses;
    // automatic responses
    bool autoRespond = true;
    bool userExists = true;
    int32_t userCredit = 500;
    bool updateOk = true;
    DeviceConfig cfg{100, false, true};

    bool ready() const override { return isReady; }
    bool remoteLive() const override { return isRemoteLive; }
    ErrorInfo statusError() const override { return ErrorInfo::none(); }
    bool submit(const CloudRequest &r) override
    {
        requests.push_back(r);
        if (!autoRespond)
            return true;
        CloudResponse resp;
        resp.id = r.id;
        resp.op = r.op;
        switch (r.op)
        {
        case CloudOp::FetchConfig:
            resp.ok = true;
            resp.config = cfg;
            break;
        case CloudOp::FetchUser:
            resp.ok = userExists;
            resp.notFound = !userExists;
            snprintf(resp.user.uid, sizeof(resp.user.uid), "%s", r.uid);
            resp.user.credit = userCredit;
            break;
        case CloudOp::UpdateUser:
            resp.ok = updateOk;
            if (!updateOk)
                resp.error = ErrorInfo::make(ErrorCode::CloudRequest, 401, "Permission denied");
            break;
        case CloudOp::WriteLog:
            resp.ok = true;
            break;
        case CloudOp::Transfer:
            snprintf(resp.user.uid, sizeof(resp.user.uid), "%s", userExists ? r.uid : r.peerUid);
            snprintf(resp.transferTo, sizeof(resp.transferTo), "%s", r.peerUid);
            resp.transferAmount = r.amount;
            if (!userExists)
                resp.notFound = true;
            else if (!PurchasePolicy::canTransfer(userCredit, r.amount))
                resp.error = ErrorInfo::make(ErrorCode::CreditLow, userCredit, "credit %ld", (long)userCredit);
            else if (!updateOk)
                resp.error = ErrorInfo::make(ErrorCode::CloudRequest, 401, "Permission denied");
            else
            {
                resp.ok = true;
                resp.user.credit = userCredit - r.amount;
            }
            break;
        default:
            break;
        }
        responses.push_back(resp);
        return true;
    }
    bool poll(CloudResponse &out) override
    {
        if (responses.empty())
            return false;
        out = responses.front();
        responses.pop_front();
        return true;
    }
    int count(CloudOp op, LogAction action = LogAction::Purchase, bool checkAction = false) const
    {
        int n = 0;
        for (auto &r : requests)
            if (r.op == op && (!checkAction || r.action == action))
                n++;
        return n;
    }
    const CloudRequest *lastOf(CloudOp op) const
    {
        for (auto it = requests.rbegin(); it != requests.rend(); ++it)
            if (it->op == op)
                return &*it;
        return nullptr;
    }
};

struct Rig
{
    FakeClock clock;
    FakeNet net;
    FakeReader reader;
    FakeCoin coin;
    FakeUi ui;
    FakeFeedback fb;
    FakeCloud cloud;
    VendTimings timings;
    VendController *ctl = nullptr;

    Rig() {}
    ~Rig() { delete ctl; }

    void start(DeviceConfig cached = DeviceConfig())
    {
        ctl = new VendController(reader, coin, ui, fb, cloud, net, clock, timings);
        ctl->begin(cached);
        step(5);
    }
    void step(int n = 1, uint32_t dtMs = 10)
    {
        for (int i = 0; i < n; i++)
        {
            clock.now += dtMs;
            coin.loop(clock.now);
            ctl->loop();
        }
    }
    void tap(uint8_t b0 = 0x04)
    {
        CardUid u;
        u.length = 4;
        u.bytes[0] = b0;
        u.bytes[1] = 0xA1;
        u.bytes[2] = 0xB2;
        u.bytes[3] = 0xC3;
        reader.cards.push_back(u);
    }
    VendController::State state() const { return ctl->state(); }
};

using S = VendController::State;

// ------------------------------------------------------------------ tests

void test_uid_hex()
{
    CardUid u;
    u.length = 4;
    u.bytes[0] = 0x04;
    u.bytes[1] = 0xa1;
    u.bytes[2] = 0x0b;
    u.bytes[3] = 0xff;
    char hex[CardUid::kHexBufferSize];
    u.toHex(hex, sizeof(hex));
    TEST_ASSERT_EQUAL_STRING("04A10BFF", hex);
}

void test_policy_keeps_original_rule()
{
    TEST_ASSERT_TRUE(PurchasePolicy::canPurchase(1, 100));
    TEST_ASSERT_FALSE(PurchasePolicy::canPurchase(0, 100));
    TEST_ASSERT_FALSE(PurchasePolicy::canPurchase(-50, 100));
    TEST_ASSERT_EQUAL_INT32(-99, PurchasePolicy::creditAfterPurchase(1, 100));
}

void test_debouncer()
{
    CardDebouncer d(2000);
    CardUid a;
    a.length = 1;
    a.bytes[0] = 1;
    CardUid b;
    b.length = 1;
    b.bytes[0] = 2;
    TEST_ASSERT_TRUE(d.accept(a, 0));
    TEST_ASSERT_FALSE(d.accept(a, 500));  // ott maradt
    TEST_ASSERT_FALSE(d.accept(a, 2400)); // still visible
    TEST_ASSERT_TRUE(d.accept(b, 2500));  // another card
    TEST_ASSERT_TRUE(d.accept(a, 2600));
    TEST_ASSERT_TRUE(d.accept(a, 5000)); // removed, tapped again
}

void test_becomes_ready_and_loads_config()
{
    Rig r;
    r.start();
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    TEST_ASSERT_EQUAL_INT32(100, r.ctl->config().price);
    TEST_ASSERT_TRUE(r.ctl->consumeConfigChanged());
    TEST_ASSERT_EQUAL(ScreenKind::Idle, r.ui.last().kind);
}

void test_not_ready_without_wifi()
{
    Rig r;
    r.net.up = false;
    r.start();
    TEST_ASSERT_EQUAL(S::NotReady, r.state());
    TEST_ASSERT_EQUAL(ErrorCode::WifiDisconnected, r.ui.last().error.code);
    r.net.up = true;
    r.step(3);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
}

void test_happy_path_purchase()
{
    Rig r;
    r.start();
    r.tap();
    r.step(2);
    // charged, pulses started
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::UpdateUser));
    TEST_ASSERT_EQUAL_INT32(400, r.cloud.lastOf(CloudOp::UpdateUser)->credit);
    TEST_ASSERT_EQUAL_STRING("04A1B2C3", r.cloud.lastOf(CloudOp::UpdateUser)->uid);
    // the log is written together with the credit (users/<UID>/log), no separate log write
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::WriteLog));
    TEST_ASSERT_EQUAL(1, r.coin.pulseTrains);
    TEST_ASSERT_EQUAL(ScreenKind::Success, r.ui.last().kind);
    TEST_ASSERT_EQUAL_INT32(400, r.ui.last().amount);

    r.step(3);
    TEST_ASSERT_EQUAL(S::WaitVendAck, r.state());
    r.coin.accepting = false; // the machine accepted the credit
    r.step();
    TEST_ASSERT_EQUAL(S::Vending, r.state());
    r.step(100, 100); // brewing
    TEST_ASSERT_EQUAL(S::Vending, r.state());
    r.coin.accepting = true; // done
    r.step();
    TEST_ASSERT_EQUAL(S::ShowResult, r.state());
    r.step(40, 100);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
}

void test_unknown_card()
{
    Rig r;
    r.start();
    r.cloud.userExists = false;
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(S::ShowResult, r.state());
    TEST_ASSERT_EQUAL(ErrorCode::CardUnknown, r.ui.last().error.code);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::UpdateUser));
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains);
}

void test_low_credit_no_write()
{
    Rig r;
    r.start();
    r.cloud.userCredit = 0;
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(ErrorCode::CreditLow, r.ui.last().error.code);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::WriteLog)); // a failed purchase is not written to the database
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::UpdateUser));
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains);
}

void test_machine_not_ready_rejects_card()
{
    Rig r;
    r.start();
    r.coin.accepting = false;
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(ErrorCode::MachineNotReady, r.ui.last().error.code);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser));
}

void test_update_failure_means_no_credit_pulse()
{
    Rig r;
    r.start();
    r.cloud.updateOk = false;
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(ErrorCode::CreditUpdateFailed, r.ui.last().error.code);
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::WriteLog, LogAction::Purchase, true));
}

void test_fetch_timeout_and_late_response_ignored()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    r.tap();
    r.step();
    TEST_ASSERT_EQUAL(S::Authorizing, r.state());
    r.clock.now += r.timings.fetchUserTimeoutMs + 1;
    r.step();
    TEST_ASSERT_EQUAL(ErrorCode::CloudTimeout, r.ui.last().error.code);

    // a late response must not trigger a charge
    CloudResponse late;
    late.id = r.cloud.lastOf(CloudOp::FetchUser)->id;
    late.op = CloudOp::FetchUser;
    late.ok = true;
    late.user.credit = 1000;
    r.cloud.responses.push_back(late);
    r.step(2);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::UpdateUser));
}

void test_vend_ack_timeout_reports_error()
{
    Rig r;
    r.start();
    r.tap();
    r.step(5);
    TEST_ASSERT_EQUAL(S::WaitVendAck, r.state());
    r.clock.now += r.timings.vendAckTimeoutMs + 1;
    r.step();
    TEST_ASSERT_EQUAL(ErrorCode::VendNotAcknowledged, r.ui.last().error.code);
}

void test_card_ignored_while_busy()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    r.tap(0x01);
    r.step();
    r.tap(0x02);
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
}

void test_free_mode_cycle()
{
    Rig r;
    r.cloud.cfg.freeMode = true;
    r.start();
    TEST_ASSERT_EQUAL(1, r.coin.pulseTrains); // the machine was ready -> gets credit right away
    r.step(3);
    r.coin.accepting = false;
    r.step();
    TEST_ASSERT_EQUAL(S::Vending, r.state());
    r.coin.accepting = true;
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::WriteLog, LogAction::FreePurchase, true));
    TEST_ASSERT_EQUAL_STRING("0", r.cloud.lastOf(CloudOp::WriteLog)->uid);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    // credit again after the retrigger delay
    r.step(150, 10);
    TEST_ASSERT_EQUAL(2, r.coin.pulseTrains);
}

void test_invalid_price_blocks_normal_mode()
{
    Rig r;
    r.cloud.cfg.price = -1;
    r.start();
    TEST_ASSERT_EQUAL(S::NotReady, r.state());
    TEST_ASSERT_EQUAL(ErrorCode::ConfigInvalid, r.ui.last().error.code);
}

void test_cached_config_kept_on_fetch_error()
{
    Rig r;
    r.cloud.autoRespond = false;
    DeviceConfig cached{150, false, true};
    r.start(cached);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    // error response to the config fetch
    CloudResponse bad;
    bad.id = r.cloud.lastOf(CloudOp::FetchConfig)->id;
    bad.op = CloudOp::FetchConfig;
    bad.ok = false;
    r.cloud.responses.push_back(bad);
    r.step();
    TEST_ASSERT_EQUAL_INT32(150, r.ctl->config().price);
}

void pushConfig(Rig &r, bool hasPrice, int32_t price, bool hasFree, bool freeMode)
{
    CloudResponse push;
    push.op = CloudOp::ConfigChanged;
    push.ok = true;
    push.hasPrice = hasPrice;
    push.hasFree = hasFree;
    push.config.price = price;
    push.config.freeMode = freeMode;
    r.cloud.responses.push_back(push);
}

void test_config_fetched_only_on_reconnect()
{
    Rig r;
    r.start();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchConfig));
    r.step(100, 10000); // ~17 minutes: no periodic fetch
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchConfig));

    r.net.up = false; // connection lost
    r.step(3);
    r.net.up = true;
    r.net.connId++; // reconnected
    r.step(3);
    TEST_ASSERT_EQUAL(2, r.cloud.count(CloudOp::FetchConfig));
}

void test_config_fetch_retried_after_failure()
{
    Rig r;
    r.cloud.autoRespond = false;
    r.start(DeviceConfig{100, false, true});
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchConfig));
    CloudResponse bad;
    bad.id = r.cloud.lastOf(CloudOp::FetchConfig)->id;
    bad.op = CloudOp::FetchConfig;
    r.cloud.responses.push_back(bad);
    r.step();
    r.step(5, 1000);
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchConfig));
    r.step(10, 1000); // again after configRetryMs
    TEST_ASSERT_EQUAL(2, r.cloud.count(CloudOp::FetchConfig));
}

void test_stream_change_applied_when_idle()
{
    Rig r;
    r.start();
    r.ctl->consumeConfigChanged();
    const size_t screensBefore = r.ui.screens.size();
    pushConfig(r, true, 120, false, false);
    r.step();
    TEST_ASSERT_EQUAL_INT32(120, r.ctl->config().price);
    TEST_ASSERT_TRUE(r.ctl->consumeConfigChanged());
    TEST_ASSERT_TRUE(r.ui.screens.size() > screensBefore); // Idle screen redrawn (new price)
    TEST_ASSERT_EQUAL_INT32(120, r.ui.last().amount);

    pushConfig(r, false, 0, true, true); // only the free field changes
    r.step();
    TEST_ASSERT_TRUE(r.ctl->config().freeMode);
    TEST_ASSERT_EQUAL_INT32(120, r.ctl->config().price);
}

void test_stream_change_deferred_during_transaction()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    r.tap();
    r.step();
    TEST_ASSERT_EQUAL(S::Authorizing, r.state());
    pushConfig(r, true, 200, false, false);
    r.step();
    TEST_ASSERT_EQUAL_INT32(100, r.ctl->config().price); // does not change during a transaction

    // the user fetch responds -> the charge still uses the old price
    CloudResponse user;
    user.id = r.cloud.lastOf(CloudOp::FetchUser)->id;
    user.op = CloudOp::FetchUser;
    user.ok = true;
    user.user.credit = 500;
    r.cloud.responses.push_back(user);
    r.step();
    TEST_ASSERT_EQUAL_INT32(400, r.cloud.lastOf(CloudOp::UpdateUser)->credit);
    TEST_ASSERT_EQUAL_INT32(100, r.ctl->config().price);

    // the transaction is aborted (timeout) -> error screen -> Idle: now the new price applies
    r.clock.now += r.timings.updateUserTimeoutMs + 1;
    r.step();
    r.step(60, 100);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    TEST_ASSERT_EQUAL_INT32(200, r.ctl->config().price);
}

int countEvents(Rig &r, const char *event, const char *contains = nullptr)
{
    int n = 0;
    for (auto &q : r.cloud.requests)
        if (q.op == CloudOp::DeviceEvent && strcmp(q.event, event) == 0 && (contains == nullptr || strstr(q.detail, contains)))
            n++;
    return n;
}

void test_event_on_config_change()
{
    Rig r;
    r.start(DeviceConfig{100, false, true}); // cache = cloud -> no event
    TEST_ASSERT_EQUAL(0, countEvents(r, "config"));
    pushConfig(r, true, 150, false, false);
    r.step();
    TEST_ASSERT_EQUAL(1, countEvents(r, "config", "100 -> 150"));
    pushConfig(r, false, 0, true, true);
    r.step();
    TEST_ASSERT_EQUAL(1, countEvents(r, "config", "normal -> free"));
}

void test_event_on_money_related_error()
{
    Rig r;
    r.start();
    r.cloud.updateOk = false; // E25
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(1, countEvents(r, "error", "E25"));
    // an unknown card is not written to the device log
    r.step(60, 100);
    r.cloud.userExists = false;
    r.tap(0x09);
    r.step(2);
    TEST_ASSERT_EQUAL(1, countEvents(r, "error"));
}

void test_reader_events_throttled()
{
    Rig r;
    r.start();
    for (int i = 0; i < 6; i++) // flapping reader
    {
        r.reader.ok = (i % 2) == 1;
        r.step(1, 1000);
    }
    TEST_ASSERT_EQUAL(1, countEvents(r, "reader")); // only the first change within 5 minutes
    r.reader.ok = true;
    r.step(1, 1000);
    r.reader.ok = false;
    r.step(1, 6UL * 60UL * 1000UL);
    TEST_ASSERT_EQUAL(2, countEvents(r, "reader"));
}

// ------------------------------------------------------------------ remote purchase (app)

void pushRemote(Rig &r, const char *id, const char *uid = "04A1B2C3", uint32_t ageMs = 1000, bool ok = true)
{
    CloudResponse req;
    req.op = CloudOp::RemoteVend;
    req.ok = ok;
    snprintf(req.remoteId, sizeof(req.remoteId), "%s", id);
    snprintf(req.user.uid, sizeof(req.user.uid), "%s", uid);
    req.remoteTsMs = static_cast<uint64_t>(r.clock.epoch()) * 1000 - ageMs;
    r.cloud.responses.push_back(req);
}

std::vector<CloudRequest> remoteStatuses(Rig &r, const char *id)
{
    std::vector<CloudRequest> out;
    for (auto &q : r.cloud.requests)
        if (q.op == CloudOp::RemoteStatus && strcmp(q.remoteId, id) == 0)
            out.push_back(q);
    return out;
}

void test_remote_happy_path()
{
    Rig r;
    r.start();
    pushRemote(r, "-Nreq1");
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
    TEST_ASSERT_EQUAL_STRING("04A1B2C3", r.cloud.lastOf(CloudOp::FetchUser)->uid);
    TEST_ASSERT_TRUE(r.ctl->consumeRemoteMarkChanged());
    r.step(2);
    TEST_ASSERT_EQUAL_INT32(400, r.cloud.lastOf(CloudOp::UpdateUser)->credit);
    TEST_ASSERT_EQUAL(1, r.coin.pulseTrains);
    TEST_ASSERT_EQUAL_STRING(msg::kRemoteVendInProgress, r.ui.last().text);

    r.step(3);
    r.coin.accepting = false;
    r.step();
    r.coin.accepting = true;
    r.step();
    TEST_ASSERT_EQUAL(S::ShowResult, r.state());

    auto st = remoteStatuses(r, "-Nreq1");
    TEST_ASSERT_EQUAL(3, (int)st.size());
    TEST_ASSERT_EQUAL(RemoteState::Accepted, st[0].remoteState);
    TEST_ASSERT_TRUE(st[0].deleteRemoteRequest); // the request is consumed right away
    TEST_ASSERT_EQUAL(RemoteState::Charged, st[1].remoteState);
    TEST_ASSERT_TRUE(st[1].charged);
    TEST_ASSERT_EQUAL_INT32(400, st[1].credit);
    TEST_ASSERT_EQUAL(RemoteState::Done, st[2].remoteState);
    TEST_ASSERT_EQUAL_INT32(400, st[2].credit);
}

void test_remote_expired_request_refused()
{
    Rig r;
    r.start();
    pushRemote(r, "-Nold", "04A1B2C3", r.timings.remoteMaxAgeMs + 1000);
    r.step();
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser));
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    auto st = remoteStatuses(r, "-Nold");
    TEST_ASSERT_EQUAL(1, (int)st.size());
    TEST_ASSERT_EQUAL(RemoteState::Error, st[0].remoteState);
    TEST_ASSERT_EQUAL(33, st[0].errorCode);
    TEST_ASSERT_TRUE(st[0].deleteRemoteRequest);
}

void test_remote_refused_while_busy()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    r.tap();
    r.step();
    TEST_ASSERT_EQUAL(S::Authorizing, r.state());
    pushRemote(r, "-Nbusy", "11223344");
    r.step();
    TEST_ASSERT_EQUAL(S::Authorizing, r.state());
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
    auto st = remoteStatuses(r, "-Nbusy");
    TEST_ASSERT_EQUAL(1, (int)st.size());
    TEST_ASSERT_EQUAL(33, st[0].errorCode);
    TEST_ASSERT_FALSE(st[0].charged);
}

void test_card_wins_over_remote_in_same_loop()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    r.tap(0x05);
    pushRemote(r, "-Nrace", "11223344");
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
    TEST_ASSERT_EQUAL_STRING("05A1B2C3", r.cloud.lastOf(CloudOp::FetchUser)->uid);
    TEST_ASSERT_EQUAL(33, remoteStatuses(r, "-Nrace")[0].errorCode);
}

void test_remote_request_not_run_twice()
{
    Rig r;
    r.start();
    pushRemote(r, "-Ndup");
    r.step(10);
    pushRemote(r, "-Ndup", "04A1B2C3", 1000 + 100); // re-delivered after a stream restart
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
    auto st = remoteStatuses(r, "-Ndup");
    TEST_ASSERT_EQUAL(RemoteState::None, st.back().remoteState);
    TEST_ASSERT_TRUE(st.back().deleteRemoteRequest);
}

void test_remote_mark_survives_reboot()
{
    Rig r;
    RemoteMark mark;
    mark.tsMs = static_cast<uint64_t>(r.clock.epoch()) * 1000;
    snprintf(mark.id, sizeof(mark.id), "-Nzz");
    r.ctl = new VendController(r.reader, r.coin, r.ui, r.fb, r.cloud, r.net, r.clock, r.timings);
    r.ctl->begin(DeviceConfig(), mark);
    r.step(5);
    TEST_ASSERT_EQUAL(S::Idle, r.state());
    pushRemote(r, "-Nzz", "04A1B2C3", 50 + 1000); // handled before the reboot
    r.step();
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser));
    pushRemote(r, "-Nzz2", "04A1B2C3", 0); // newer request
    r.step();
    TEST_ASSERT_EQUAL(1, r.cloud.count(CloudOp::FetchUser));
}

void test_remote_update_failure_means_no_pulse()
{
    Rig r;
    r.start();
    r.cloud.updateOk = false;
    pushRemote(r, "-Nfail");
    r.step(3);
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains);
    auto st = remoteStatuses(r, "-Nfail");
    TEST_ASSERT_EQUAL(RemoteState::Error, st.back().remoteState);
    TEST_ASSERT_EQUAL(25, st.back().errorCode);
    TEST_ASSERT_FALSE(st.back().charged);
}

void test_remote_charged_but_inhibit_raised()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    pushRemote(r, "-Ne53");
    r.step();
    CloudResponse user;
    user.id = r.cloud.lastOf(CloudOp::FetchUser)->id;
    user.op = CloudOp::FetchUser;
    user.ok = true;
    user.user.credit = 500;
    r.cloud.responses.push_back(user);
    r.step();
    CloudResponse upd;
    upd.id = r.cloud.lastOf(CloudOp::UpdateUser)->id;
    upd.op = CloudOp::UpdateUser;
    upd.ok = true;
    r.cloud.responses.push_back(upd);
    r.coin.accepting = false; // the machine got busy meanwhile
    r.step();
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains);
    auto st = remoteStatuses(r, "-Ne53");
    TEST_ASSERT_EQUAL(RemoteState::Error, st.back().remoteState);
    TEST_ASSERT_EQUAL(53, st.back().errorCode);
    TEST_ASSERT_TRUE(st.back().charged);
    TEST_ASSERT_EQUAL_INT32(400, st.back().credit);
}

void test_remote_refused_in_free_mode_and_when_machine_busy()
{
    Rig r;
    r.cloud.cfg.freeMode = true;
    r.start();
    r.step(3);
    r.coin.accepting = false; // free credit taken, drink in progress
    r.step(2);
    pushRemote(r, "-Nfree");
    r.step();
    TEST_ASSERT_EQUAL(33, remoteStatuses(r, "-Nfree")[0].errorCode);

    Rig r2;
    r2.start();
    r2.coin.accepting = false;
    pushRemote(r2, "-Ninh");
    r2.step();
    TEST_ASSERT_EQUAL(50, remoteStatuses(r2, "-Ninh")[0].errorCode);
    TEST_ASSERT_EQUAL(0, r2.cloud.count(CloudOp::FetchUser));
    TEST_ASSERT_EQUAL(S::Idle, r2.state()); // no error screen for a refused app request
}

void test_remote_malformed_request()
{
    Rig r;
    r.start();
    pushRemote(r, "-Nbad", "", 1000, false);
    r.step();
    TEST_ASSERT_EQUAL(24, remoteStatuses(r, "-Nbad")[0].errorCode);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser));
}

void test_remote_cuts_result_screen_short()
{
    Rig r;
    r.start();
    r.cloud.userCredit = 0;
    r.tap();
    r.step(2);
    TEST_ASSERT_EQUAL(S::ShowResult, r.state()); // E31 screen of someone else
    r.cloud.userCredit = 500;
    pushRemote(r, "-Nnext");
    r.step();
    TEST_ASSERT_EQUAL(2, r.cloud.count(CloudOp::FetchUser));
}

// ------------------------------------------------------------------ credit transfer (app)

void pushTransfer(Rig &r, const char *id, const char *to, int32_t amount, const char *from = "04A1B2C3")
{
    pushRemote(r, id, from);
    CloudResponse &req = r.cloud.responses.back();
    req.transfer = true;
    snprintf(req.transferTo, sizeof(req.transferTo), "%s", to);
    req.transferAmount = amount;
}

void test_transfer_policy()
{
    TEST_ASSERT_TRUE(PurchasePolicy::canTransfer(500, 500));
    TEST_ASSERT_FALSE(PurchasePolicy::canTransfer(400, 500)); // no going negative, unlike a purchase
    TEST_ASSERT_FALSE(PurchasePolicy::canTransfer(500, 150)); // whole hundreds only
    TEST_ASSERT_FALSE(PurchasePolicy::canTransfer(500, 0));
    TEST_ASSERT_FALSE(PurchasePolicy::canTransfer(500, -100));
}

void test_transfer_happy_path()
{
    Rig r;
    r.start();
    pushTransfer(r, "-Nxfer", "11223344", 200);
    r.step();
    const CloudRequest *t = r.cloud.lastOf(CloudOp::Transfer);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_STRING("04A1B2C3", t->uid);
    TEST_ASSERT_EQUAL_STRING("11223344", t->peerUid);
    TEST_ASSERT_EQUAL_INT32(200, t->amount);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser));
    r.step();
    TEST_ASSERT_EQUAL(S::ShowResult, r.state());
    TEST_ASSERT_EQUAL(0, r.coin.pulseTrains); // no drink
    TEST_ASSERT_EQUAL_STRING("Átutalva: 200 Ft", r.ui.last().text);
    TEST_ASSERT_EQUAL_INT32(300, r.ui.last().amount);

    auto st = remoteStatuses(r, "-Nxfer");
    TEST_ASSERT_EQUAL(2, (int)st.size());
    TEST_ASSERT_EQUAL(RemoteState::Accepted, st[0].remoteState);
    TEST_ASSERT_TRUE(st[0].deleteRemoteRequest);
    TEST_ASSERT_EQUAL(RemoteState::Done, st[1].remoteState);
    TEST_ASSERT_TRUE(st[1].charged);
    TEST_ASSERT_EQUAL_INT32(300, st[1].credit);
}

void test_transfer_low_credit_and_unknown_card()
{
    Rig r;
    r.start();
    r.cloud.userCredit = 100;
    pushTransfer(r, "-Nlow", "11223344", 200);
    r.step(2);
    auto st = remoteStatuses(r, "-Nlow");
    TEST_ASSERT_EQUAL(RemoteState::Error, st.back().remoteState);
    TEST_ASSERT_EQUAL(31, st.back().errorCode);
    TEST_ASSERT_FALSE(st.back().charged);

    Rig r2;
    r2.start();
    r2.cloud.userExists = false;
    pushTransfer(r2, "-Nunk", "11223344", 200);
    r2.step(2);
    TEST_ASSERT_EQUAL(30, remoteStatuses(r2, "-Nunk").back().errorCode);

    Rig r3;
    r3.start();
    r3.cloud.updateOk = false;
    pushTransfer(r3, "-Nfail", "11223344", 200);
    r3.step(2);
    TEST_ASSERT_EQUAL(25, remoteStatuses(r3, "-Nfail").back().errorCode);
    TEST_ASSERT_FALSE(remoteStatuses(r3, "-Nfail").back().charged);
}

void test_transfer_invalid_request_refused()
{
    Rig r;
    r.start();
    pushTransfer(r, "-Nodd", "11223344", 150);
    r.step();
    TEST_ASSERT_EQUAL(24, remoteStatuses(r, "-Nodd")[0].errorCode);
    pushTransfer(r, "-Nself", "04A1B2C3", 200);
    r.step();
    TEST_ASSERT_EQUAL(24, remoteStatuses(r, "-Nself")[0].errorCode);
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::Transfer));
    TEST_ASSERT_EQUAL(S::Idle, r.state());
}

void test_transfer_blocks_cards_and_requests()
{
    Rig r;
    r.start();
    r.cloud.autoRespond = false;
    pushTransfer(r, "-Nslow", "11223344", 200);
    r.step();
    TEST_ASSERT_EQUAL(S::Transferring, r.state());
    r.tap();
    r.step();
    pushRemote(r, "-Nzbuy");
    r.step();
    TEST_ASSERT_EQUAL(0, r.cloud.count(CloudOp::FetchUser)); // the sender's credit cannot change in between
    TEST_ASSERT_EQUAL(33, remoteStatuses(r, "-Nzbuy")[0].errorCode);
    TEST_ASSERT_EQUAL(S::Transferring, r.state());

    r.step(1, r.timings.updateUserTimeoutMs);
    TEST_ASSERT_EQUAL(S::ShowResult, r.state());
    TEST_ASSERT_EQUAL(23, remoteStatuses(r, "-Nslow").back().errorCode);
}

void test_transfer_works_in_free_mode_and_with_inhibit()
{
    Rig r;
    r.cloud.cfg.freeMode = true;
    r.coin.accepting = false; // a drink in progress: no credit needed for a transfer
    r.start();
    r.step(3);
    pushTransfer(r, "-Nfree", "11223344", 100);
    r.step(2);
    TEST_ASSERT_EQUAL(RemoteState::Done, remoteStatuses(r, "-Nfree").back().remoteState);
}

const CloudRequest *lastPresence(Rig &r) { return r.cloud.lastOf(CloudOp::RemoteOnline); }

void test_remote_presence_heartbeat()
{
    Rig r;
    r.start();
    TEST_ASSERT_NOT_NULL(lastPresence(r)); // first one right away (still starting: not ready)
    r.step(25, 100);                       // Idle -> ready, written after the min gap
    TEST_ASSERT_TRUE(lastPresence(r)->remoteReady);
    TEST_ASSERT_EQUAL_INT32(100, lastPresence(r)->price);
    const int first = r.cloud.count(CloudOp::RemoteOnline);

    r.step(50, 100); // 5 s, nothing changed
    TEST_ASSERT_EQUAL(first, r.cloud.count(CloudOp::RemoteOnline));

    r.coin.accepting = false; // machine busy -> not ready, reported after the min gap
    r.step(30, 100);
    TEST_ASSERT_EQUAL(first + 1, r.cloud.count(CloudOp::RemoteOnline));
    TEST_ASSERT_FALSE(lastPresence(r)->remoteReady);

    r.step(65, 1000); // periodic heartbeat
    TEST_ASSERT_EQUAL(first + 2, r.cloud.count(CloudOp::RemoteOnline));

    r.coin.accepting = true;
    r.cloud.isRemoteLive = false; // no request stream -> not ready
    r.step(30, 100);
    TEST_ASSERT_FALSE(lastPresence(r)->remoteReady);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_uid_hex);
    RUN_TEST(test_policy_keeps_original_rule);
    RUN_TEST(test_debouncer);
    RUN_TEST(test_becomes_ready_and_loads_config);
    RUN_TEST(test_not_ready_without_wifi);
    RUN_TEST(test_happy_path_purchase);
    RUN_TEST(test_unknown_card);
    RUN_TEST(test_low_credit_no_write);
    RUN_TEST(test_machine_not_ready_rejects_card);
    RUN_TEST(test_update_failure_means_no_credit_pulse);
    RUN_TEST(test_fetch_timeout_and_late_response_ignored);
    RUN_TEST(test_vend_ack_timeout_reports_error);
    RUN_TEST(test_card_ignored_while_busy);
    RUN_TEST(test_free_mode_cycle);
    RUN_TEST(test_invalid_price_blocks_normal_mode);
    RUN_TEST(test_cached_config_kept_on_fetch_error);
    RUN_TEST(test_config_fetched_only_on_reconnect);
    RUN_TEST(test_config_fetch_retried_after_failure);
    RUN_TEST(test_stream_change_applied_when_idle);
    RUN_TEST(test_stream_change_deferred_during_transaction);
    RUN_TEST(test_event_on_config_change);
    RUN_TEST(test_event_on_money_related_error);
    RUN_TEST(test_reader_events_throttled);
    RUN_TEST(test_remote_happy_path);
    RUN_TEST(test_remote_expired_request_refused);
    RUN_TEST(test_remote_refused_while_busy);
    RUN_TEST(test_card_wins_over_remote_in_same_loop);
    RUN_TEST(test_remote_request_not_run_twice);
    RUN_TEST(test_remote_mark_survives_reboot);
    RUN_TEST(test_remote_update_failure_means_no_pulse);
    RUN_TEST(test_remote_charged_but_inhibit_raised);
    RUN_TEST(test_remote_refused_in_free_mode_and_when_machine_busy);
    RUN_TEST(test_remote_malformed_request);
    RUN_TEST(test_remote_cuts_result_screen_short);
    RUN_TEST(test_remote_presence_heartbeat);
    RUN_TEST(test_transfer_policy);
    RUN_TEST(test_transfer_happy_path);
    RUN_TEST(test_transfer_low_credit_and_unknown_card);
    RUN_TEST(test_transfer_invalid_request_refused);
    RUN_TEST(test_transfer_blocks_cards_and_requests);
    RUN_TEST(test_transfer_works_in_free_mode_and_with_inhibit);
    return UNITY_END();
}
