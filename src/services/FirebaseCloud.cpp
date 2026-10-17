#include "FirebaseCloud.h"

#include <ArduinoJson.h>
#include <FirebaseClient.h>
#include <Log.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "CLOUD";

    // The FirebaseClient objects are static (the library keeps references to them)
    WiFiClientSecure sslClient;
    AsyncClientClass aClient(sslClient);
    // separate connection for the config stream (always open, does not block the requests)
    WiFiClientSecure streamSslClient;
    AsyncClientClass streamClient(streamSslClient);

    // Firebase sends a keep-alive event every ~30 s; the library reconnects by itself after 40 s.
    // This is our own backup supervision: after this much silence the stream is restarted.
    constexpr uint32_t kStreamStallMs = 90000;
    constexpr uint32_t kStreamRestartDelayMs = 10000;
    // a device event may wait this long while offline (e.g. the boot event waiting for WiFi)
    constexpr uint32_t kEventTtlMs = 30UL * 60UL * 1000UL;
    FirebaseApp app;
    RealtimeDatabase db;
    UserAuth userAuth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD, 3000 /* token lifetime, s */);
    NoAuth noAuth;

    FirebaseCloud *gCloud = nullptr;

    bool useUserAuth() { return FIREBASE_USER_EMAIL[0] != '\0'; }

    void authCallback(AsyncResult &r)
    {
        if (gCloud == nullptr)
            return;
        if (r.isError())
            gCloud->onAuthEvent(r.error().code(), r.error().message().c_str(), true);
        if (r.isEvent())
            gCloud->onAuthEvent(r.eventLog().code(), r.eventLog().message().c_str(), false);
        if (r.isDebug())
            LOGD(TAG, "auth debug: %s", r.debug().c_str());
    }

    void streamCallback(AsyncResult &r)
    {
        if (gCloud == nullptr)
            return;
        if (r.isError())
            gCloud->onStreamError(r.error().code(), r.error().message().c_str());
        if (r.available())
        {
            RealtimeDatabaseResult &st = r.to<RealtimeDatabaseResult>();
            if (st.isStream())
                gCloud->onStreamEvent(st.event(), st.dataPath(), st.to<String>());
        }
    }

    // "/conf/" -> "/conf"
    String streamPath()
    {
        String p = DB_CONF_PATH;
        while (p.length() > 1 && p.endsWith("/"))
            p.remove(p.length() - 1);
        return p;
    }

    // log-type request: no response, the worker may retry / drop it
    inline bool isFireAndForget(const CloudRequest &req)
    {
        return req.op == CloudOp::WriteLog || req.op == CloudOp::DeviceEvent;
    }

    void logLost(const char *why, const CloudRequest &req, const char *err = "")
    {
        if (req.op == CloudOp::DeviceEvent)
            LOGE(TAG, "LOST device event (%s): %s: %s %s", why, req.event, req.detail, err);
        else
            LOGE(TAG, "LOST log (%s): uid=%s action=%ld %s", why, req.uid, (long)req.action, err);
    }

    inline bool expired(const CloudRequest &req) { return static_cast<int32_t>(millis() - req.deadlineMs) >= 0; }

    const char *opName(CloudOp op)
    {
        switch (op)
        {
        case CloudOp::FetchUser: return "Fetch user";
        case CloudOp::UpdateUser: return "Update credit";
        case CloudOp::WriteLog: return "Write log";
        case CloudOp::FetchConfig: return "Fetch config";
        case CloudOp::ConfigChanged: return "Config change (stream)";
        case CloudOp::DeviceEvent: return "Write device event";
        }
        return "?";
    }

    // number from an RTDB primitive value (e.g. "100" or "\"100\"")
    bool parseInt(const String &raw, int32_t &out)
    {
        String s = raw;
        s.trim();
        if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2)
            s = s.substring(1, s.length() - 1);
        if (s.isEmpty() || s == "null")
            return false;
        char *end = nullptr;
        const long v = strtol(s.c_str(), &end, 10);
        if (end == s.c_str())
            return false;
        out = static_cast<int32_t>(v);
        return true;
    }
} // namespace

FirebaseCloud::FirebaseCloud(WifiNetwork &wifi, SystemClock &clock) : wifi_(wifi), clock_(clock) {}

void FirebaseCloud::begin()
{
    gCloud = this;
    requests_ = xQueueCreate(settings::kCloudRequestQueueLen, sizeof(CloudRequest));
    responses_ = xQueueCreate(settings::kCloudResponseQueueLen, sizeof(CloudResponse));
    if (requests_ == nullptr || responses_ == nullptr)
    {
        LOGE(TAG, "Creating the queues failed (out of memory?)");
        return;
    }
    wifi_.begin();
    xTaskCreatePinnedToCore(&FirebaseCloud::taskEntry, "cloud", settings::kCloudTaskStackBytes, this,
                            settings::kCloudTaskPriority, &task_, settings::kCloudTaskCore);
}

bool FirebaseCloud::submit(const CloudRequest &request)
{
    return requests_ != nullptr && xQueueSend(requests_, &request, 0) == pdTRUE;
}

bool FirebaseCloud::poll(CloudResponse &response)
{
    return responses_ != nullptr && xQueueReceive(responses_, &response, 0) == pdTRUE;
}

ErrorInfo FirebaseCloud::statusError() const
{
    portENTER_CRITICAL(&lock_);
    ErrorInfo copy = statusError_;
    portEXIT_CRITICAL(&lock_);
    return copy;
}

void FirebaseCloud::setStatusError(const ErrorInfo &e)
{
    portENTER_CRITICAL(&lock_);
    statusError_ = e;
    portEXIT_CRITICAL(&lock_);
}

void FirebaseCloud::onAuthEvent(int code, const char *message, bool isError)
{
    if (isError)
    {
        LOGE(TAG, "Sign-in error: %s (code %d)", message, code);
        setStatusError(ErrorInfo::make(ErrorCode::CloudAuth, code, "Sign-in: %s (code %d)", message, code));
        return;
    }
    LOGI(TAG, "Auth: %s", message);
    if (code == auth_event_ready)
        setStatusError(ErrorInfo::none());
}

// ------------------------------------------------------------------ task

void FirebaseCloud::taskEntry(void *arg)
{
    static_cast<FirebaseCloud *>(arg)->run();
}

void FirebaseCloud::startApp()
{
    sslClient.setInsecure(); // the Firebase certificate chain may change; authentication is provided by the token
    sslClient.setHandshakeTimeout(settings::kFirebaseHandshakeTimeoutS);
    aClient.setSyncSendTimeout(settings::kFirebaseSyncTimeoutS);
    aClient.setSyncReadTimeout(settings::kFirebaseSyncTimeoutS);

    if (useUserAuth())
    {
        LOGI(TAG, "Starting Firebase (user: %s)", FIREBASE_USER_EMAIL);
        initializeApp(aClient, app, getAuth(userAuth), authCallback, "auth");
    }
    else
    {
        LOGW(TAG, "Starting Firebase without authentication (NoAuth) - only works with open database rules");
        initializeApp(aClient, app, getAuth(noAuth), authCallback, "auth");
    }
    app.getApp<RealtimeDatabase>(db);
    db.url(FIREBASE_DATABASE_URL);
    appStarted_ = true;
}

void FirebaseCloud::run()
{
    esp_task_wdt_add(nullptr);
    LOGI(TAG, "Network task running (core %d)", xPortGetCoreID());

    for (;;)
    {
        esp_task_wdt_reset();
        wifi_.maintain(millis());
        const bool online = wifi_.connected();
        trackWifiEvents(online);

        if (online)
        {
            clock_.startSync(settings::kTimezone, settings::kNtpServer1, settings::kNtpServer2);
            if (!appStarted_)
                startApp();
            app.loop();
        }
        if (netClient_ != nullptr)
            netClient_->onNetTick(online);
        maintainStream(online, online && appStarted_ && app.ready());

        const bool nowReady = online && appStarted_ && app.ready();
        ready_.store(nowReady);
        if (nowReady != wasReady_)
        {
            wasReady_ = nowReady;
            if (nowReady)
                LOGI(TAG, "Firebase ready");
            else
                LOGW(TAG, "Firebase unavailable (WiFi: %s)", online ? "up" : "down");
        }

        if (nowReady)
        {
            CloudRequest req;
            if (xQueueReceive(requests_, &req, pdMS_TO_TICKS(20)) == pdTRUE)
                process(req);
        }
        else
        {
            rejectExpiredHead();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// Without a connection, expired (non-log) requests are answered with an error right away
void FirebaseCloud::rejectExpiredHead()
{
    CloudRequest req;
    if (xQueuePeek(requests_, &req, 0) != pdTRUE || !expired(req))
        return;
    xQueueReceive(requests_, &req, 0);
    if (isFireAndForget(req))
    {
        logLost("offline", req);
        return;
    }
    CloudResponse resp;
    resp.id = req.id;
    resp.op = req.op;
    resp.error = ErrorInfo::make(ErrorCode::CloudNotReady, 0, "%s: no connection, the request was not executed", opName(req.op));
    respond(resp);
}

void FirebaseCloud::respond(const CloudResponse &resp)
{
    if (xQueueSend(responses_, &resp, pdMS_TO_TICKS(100)) != pdTRUE)
        LOGE(TAG, "Response queue full - response dropped (op=%u)", static_cast<unsigned>(resp.op));
}

void FirebaseCloud::process(CloudRequest &req)
{
    [[maybe_unused]] const uint32_t started = millis();

    if (expired(req))
    {
        if (isFireAndForget(req))
        {
            logLost("expired", req);
            return;
        }
        // An expired request must NOT be executed (especially a charge) - the controller no longer waits for it
        CloudResponse resp;
        resp.id = req.id;
        resp.op = req.op;
        resp.error = ErrorInfo::make(ErrorCode::CloudTimeout, 0, "%s: the request waited too long in the queue, not executed",
                                     opName(req.op));
        respond(resp);
        return;
    }

    if (isFireAndForget(req))
    {
        // event created before the time sync: fill in the timestamp now; if there is no exact time yet, later
        if (req.timestamp < 1700000000u)
        {
            if (!clock_.synced())
            {
                vTaskDelay(pdMS_TO_TICKS(200));
                xQueueSendToBack(requests_, &req, 0);
                return;
            }
            req.timestamp = clock_.epoch();
        }

        ErrorInfo err;
        const bool ok = req.op == CloudOp::DeviceEvent ? writeDeviceEvent(req, err) : writeLog(req, err);
        if (!ok)
        {
            req.attempt++;
            if (req.attempt < settings::kLogMaxAttempts)
            {
                LOGW(TAG, "%s failed (attempt %u): %s - retrying", opName(req.op), req.attempt, err.detail);
                vTaskDelay(pdMS_TO_TICKS(500));
                xQueueSendToBack(requests_, &req, 0);
            }
            else
            {
                logLost("repeated failure", req, err.detail);
            }
        }
        return;
    }

    CloudResponse resp;
    resp.id = req.id;
    resp.op = req.op;
    switch (req.op)
    {
    case CloudOp::FetchUser: fetchUser(req, resp); break;
    case CloudOp::UpdateUser: updateUser(req, resp); break;
    case CloudOp::FetchConfig: fetchConfig(req, resp); break;
    case CloudOp::WriteLog:
    case CloudOp::DeviceEvent:
    case CloudOp::ConfigChanged: break;
    }
    LOGD(TAG, "%s: %s (%lu ms)", opName(req.op), resp.ok ? "OK" : "FAILED", (unsigned long)(millis() - started));
    respond(resp);
}

// ------------------------------------------------------------------ device event log

void FirebaseCloud::trackWifiEvents(bool online)
{
    const uint32_t now = millis();
    if (online == eventOnline_)
        return;
    eventOnline_ = online;
    if (!online)
    {
        offlineSinceMs_ = now;
        return;
    }

    char ssid[33];
    wifi_.ssid(ssid, sizeof(ssid));
    CloudRequest req;
    if (!everOnline_)
        req = makeDeviceEvent(now + kEventTtlMs, 0, "wifi", "connected: %s (%d dBm)", ssid, wifi_.rssi());
    else
        req = makeDeviceEvent(now + kEventTtlMs, 0, "wifi", "reconnected: %s (%d dBm), offline for %lu s", ssid, wifi_.rssi(),
                              (unsigned long)((now - offlineSinceMs_) / 1000));
    everOnline_ = true;
    xQueueSendToBack(requests_, &req, 0);
}

bool FirebaseCloud::writeDeviceEvent(const CloudRequest &req, ErrorInfo &err)
{
    // /Log/<epoch> = {"e": event, "d": details}; the key is strictly increasing
    uint32_t key = req.timestamp;
    if (key <= lastEventKey_)
        key = lastEventKey_ + 1;

    const String path = String(DB_LOG_PATH) + String(key);
    JsonDocument doc;
    doc["e"] = req.event;
    doc["d"] = req.detail;
    String body;
    serializeJson(doc, body);

    LOGI(TAG, "PUT %s %s", path.c_str(), body.c_str());
    db.set<object_t>(aClient, path, object_t(body));
    if (!lastCallOk(opName(req.op), path.c_str(), err))
        return false;
    lastEventKey_ = key;
    return true;
}

// ------------------------------------------------------------------ config stream

void FirebaseCloud::maintainStream(bool online, bool ready)
{
    const uint32_t now = millis();

    // fresh stream after a reconnect (the old TLS connection is dead)
    if (online && !wasOnline_ && streamActive_)
        stopStream("WiFi reconnect");
    wasOnline_ = online;

    if (!ready)
    {
        if (streamActive_ && !online)
            stopStream("nincs WiFi");
        liveUpdates_.store(false);
        return;
    }

    if (!streamActive_)
    {
        if (static_cast<int32_t>(now - nextStreamStartMs_) >= 0)
            startStream();
    }
    else if (streamRestart_)
    {
        stopStream("closed by the server (cancel / auth_revoked)");
    }
    else if (now - lastStreamEventMs_ > kStreamStallMs)
    {
        stopStream("no event (keep-alive) for 90 s");
    }

    liveUpdates_.store(streamActive_ && now - lastStreamEventMs_ <= kStreamStallMs && lastStreamEventMs_ != 0);
}

void FirebaseCloud::startStream()
{
    streamSslClient.setInsecure();
    streamSslClient.setHandshakeTimeout(settings::kFirebaseHandshakeTimeoutS);
    streamClient.setSSEFilters("put,patch,keep-alive,cancel,auth_revoked");

    const String path = streamPath();
    LOGI(TAG, "Starting config stream: %s", path.c_str());
    db.get(streamClient, path, streamCallback, true /* SSE */, "confStream");
    streamActive_ = true;
    streamRestart_ = false;
    lastStreamEventMs_ = millis(); // the supervision counts from here
}

void FirebaseCloud::stopStream(const char *reason)
{
    LOGW(TAG, "Config stream stopped: %s", reason);
    streamClient.stopAsync(true);
    streamActive_ = false;
    liveUpdates_.store(false);
    nextStreamStartMs_ = millis() + kStreamRestartDelayMs;
}

void FirebaseCloud::onStreamError(int code, const char *message)
{
    LOGW(TAG, "Config stream error: %s (code %d)", message, code);
}

void FirebaseCloud::onStreamEvent(const String &event, const String &path, const String &data)
{
    lastStreamEventMs_ = millis();

    if (event.indexOf("keep-alive") >= 0)
    {
        LOGD(TAG, "Config stream: keep-alive");
        return;
    }
    if (event.indexOf("cancel") >= 0 || event.indexOf("auth_revoked") >= 0)
    {
        LOGW(TAG, "Config stream: %s", event.c_str());
        streamRestart_ = true;
        return;
    }
    const bool isPut = event.indexOf("put") >= 0;
    const bool isPatch = event.indexOf("patch") >= 0;
    if (!isPut && !isPatch)
        return;

    CloudResponse resp;
    resp.op = CloudOp::ConfigChanged;
    resp.ok = true;

    if (path == "/")
    {
        // put "/" = the whole /conf node (e.g. when the stream starts); patch "/" = only the changed fields
        JsonDocument doc;
        if (deserializeJson(doc, data) || !doc.is<JsonObject>())
        {
            LOGW(TAG, "Config stream: nem objektum az adat (%s): %.80s", event.c_str(), data.c_str());
            return;
        }
        if (doc["price"].is<int32_t>())
        {
            resp.hasPrice = true;
            resp.config.price = doc["price"].as<int32_t>();
        }
        else if (isPut)
        {
            LOGW(TAG, "Config stream: the price field is missing or not a number");
        }
        if (isPut || doc["free"].is<bool>())
        {
            resp.hasFree = true;
            resp.config.freeMode = doc["free"] | false; // missing = not free (same as the original)
        }
    }
    else if (path == "/price")
    {
        int32_t price = 0;
        if (!parseInt(data, price))
        {
            LOGW(TAG, "Config stream: invalid price value: %s", data.c_str());
            return;
        }
        resp.hasPrice = true;
        resp.config.price = price;
    }
    else if (path == "/free")
    {
        String v = data;
        v.trim();
        resp.hasFree = true;
        resp.config.freeMode = (v == "true");
    }
    else
    {
        LOGD(TAG, "Config stream: %s %s ignored", event.c_str(), path.c_str());
        return; // e.g. /coffeeBeans/... - does not concern the device
    }

    LOGI(TAG, "Config stream (%s %s): %s%s%s", event.c_str(), path.c_str(),
         resp.hasPrice ? (String("price=") + resp.config.price + " ").c_str() : "",
         resp.hasFree ? "free=" : "", resp.hasFree ? (resp.config.freeMode ? "true" : "false") : "");
    respond(resp);
}

// ------------------------------------------------------------------ operations

bool FirebaseCloud::lastCallOk(const char *op, const char *path, ErrorInfo &out)
{
    const int code = aClient.lastError().code();
    if (code == 0)
        return true;

    ErrorCode ec = ErrorCode::CloudRequest;
    if (code == FIREBASE_ERROR_TCP_RECEIVE_TIMEOUT || code == FIREBASE_ERROR_HTTP_CODE_REQUEST_TIMEOUT ||
        code == FIREBASE_ERROR_HTTP_CODE_GATEWAY_TIMEOUT)
        ec = ErrorCode::CloudTimeout;
    else if (code == FIREBASE_ERROR_HTTP_CODE_UNAUTHORIZED || code == FIREBASE_ERROR_HTTP_CODE_FORBIDDEN)
        ec = ErrorCode::CloudAuth;

    const String message = aClient.lastError().message();
    out = ErrorInfo::make(ec, code, "%s (%s): %s [code %d]", op, path, message.c_str(), code);
    LOGE(TAG, "%s", out.detail);
    return false;
}

void FirebaseCloud::fetchUser(const CloudRequest &req, CloudResponse &resp)
{
    // Only the needed fields: users/<UID>/credit and users/<UID>/name as separate leaf queries.
    // (shallow=true replaces EVERY child with "true", numbers included - useless here. Fetching the whole
    // node would also download the users/<UID>/log history, which grows continuously.)
    const String base = String(DB_USER_PATH) + req.uid;
    snprintf(resp.user.uid, sizeof(resp.user.uid), "%s", req.uid);

    const String creditPath = base + "/credit";
    LOGI(TAG, "GET %s", creditPath.c_str());
    const String creditRaw = db.get<String>(aClient, creditPath);
    if (!lastCallOk(opName(req.op), creditPath.c_str(), resp.error))
        return;

    int32_t credit = 0;
    if (!parseInt(creditRaw, credit))
    {
        // no credit: non-existent user or broken record? (an extra request only in this rare case)
        DatabaseOptions options;
        options.shallow = true;
        const String exists = db.get<String>(aClient, base, options);
        if (!lastCallOk(opName(req.op), base.c_str(), resp.error))
            return;
        if (exists.isEmpty() || exists == "null")
        {
            resp.notFound = true;
            LOGW(TAG, "No such user: %s", req.uid);
            return;
        }
        resp.error = ErrorInfo::make(ErrorCode::CloudData, 0, "%s: the 'credit' field is missing or not an integer ('%s')",
                                     base.c_str(), creditRaw.c_str());
        return;
    }
    resp.user.credit = credit;

    // the name is optional (display only) - an error with it does not block the purchase
    const String namePath = base + "/name";
    const String nameRaw = db.get<String>(aClient, namePath);
    if (aClient.lastError().code() == 0 && nameRaw.length() > 0 && nameRaw != "null")
    {
        JsonDocument doc;
        if (!deserializeJson(doc, nameRaw) && doc.is<const char *>())
            snprintf(resp.user.name, sizeof(resp.user.name), "%s", doc.as<const char *>());
        else
            snprintf(resp.user.name, sizeof(resp.user.name), "%s", nameRaw.c_str());
    }
    resp.ok = true;
}

void FirebaseCloud::updateUser(const CloudRequest &req, CloudResponse &resp)
{
    // Credit + log in ONE atomic PATCH: users/<UID> {"credit": x, "log/<epoch>": x}
    // The log value is the credit after the purchase (admin top-ups are logged the same way).
    // Key: epoch seconds, strictly increasing - entries in the same second do not overwrite each other.
    uint32_t key = req.timestamp;
    if (key <= lastLogKey_)
        key = lastLogKey_ + 1;

    const String path = String(DB_USER_PATH) + req.uid;
    JsonDocument doc;
    doc["credit"] = req.credit;
    doc[String("log/") + String(key)] = req.credit;
    String body;
    serializeJson(doc, body);

    LOGI(TAG, "PATCH %s %s", path.c_str(), body.c_str());
    db.update(aClient, path, object_t(body));
    if (!lastCallOk(opName(req.op), path.c_str(), resp.error))
        return;

    lastLogKey_ = key;
    snprintf(resp.user.uid, sizeof(resp.user.uid), "%s", req.uid);
    resp.user.credit = req.credit;
    resp.ok = true;
}

bool FirebaseCloud::writeLog(const CloudRequest &req, ErrorInfo &err)
{
    // The only separately logged event is a free purchase: a daily counter with a server-side increment
    //   /stat/free/<YYYYMMDD> += 1
    if (req.action != LogAction::FreePurchase)
    {
        LOGW(TAG, "Unsupported log event (action=%ld) - skipped", (long)req.action);
        return true;
    }

    const time_t ts = static_cast<time_t>(req.timestamp);
    struct tm local;
    localtime_r(&ts, &local);
    char day[9];
    strftime(day, sizeof(day), "%Y%m%d", &local);

    const String path = String(DB_STAT_PATH) + "free/" + day;
    LOGI(TAG, "PUT %s += 1", path.c_str());
    db.set<object_t>(aClient, path, object_t("{\".sv\":{\"increment\":1}}"));
    return lastCallOk(opName(req.op), path.c_str(), err);
}

void FirebaseCloud::fetchConfig(const CloudRequest &req, CloudResponse &resp)
{
    const String pricePath = String(DB_CONF_PATH) + "price";
    const String priceRaw = db.get<String>(aClient, pricePath);
    if (!lastCallOk(opName(req.op), pricePath.c_str(), resp.error))
        return;
    int32_t price = 0;
    if (!parseInt(priceRaw, price))
    {
        resp.error = ErrorInfo::make(ErrorCode::ConfigInvalid, 0, "%s: missing or not a number ('%s')", pricePath.c_str(),
                                     priceRaw.c_str());
        LOGE(TAG, "%s", resp.error.detail);
        return;
    }

    const String freePath = String(DB_CONF_PATH) + "free";
    String freeRaw = db.get<String>(aClient, freePath);
    if (!lastCallOk(opName(req.op), freePath.c_str(), resp.error))
        return;
    freeRaw.trim();

    resp.config.price = price;
    resp.config.freeMode = (freeRaw == "true"); // missing value = not free (same as the original)
    resp.config.loaded = true;
    resp.ok = true;
    LOGI(TAG, "Config: price=%ld, free=%s", (long)price, freeRaw.c_str());
}
