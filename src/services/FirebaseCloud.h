#pragma once

#include <Arduino.h>
#include <Interfaces.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "NetTaskClient.h"
#include "SystemClock.h"
#include "WifiNetwork.h"

// Firebase Realtime Database access (mobizt/FirebaseClient) in a separate FreeRTOS task.
//
// The task also maintains the WiFi connection, starts the time sync and signs in to Firebase, so every
// slow / blocking network operation runs on core 0 and the main loop (display, reader, machine
// interface) always stays responsive. Communication happens through two FreeRTOS queues:
//   submit() -> request queue -> task (synchronous RTDB call) -> response queue -> poll()
class FirebaseCloud : public ICloud
{
public:
    FirebaseCloud(WifiNetwork &wifi, SystemClock &clock);

    // Add-on called in every round of the network task (e.g. OTA). Must be set before begin().
    void setNetTaskClient(INetTaskClient *client) { netClient_ = client; }

    void begin();

    bool ready() const override { return ready_.load(); }
    bool submit(const CloudRequest &request) override;
    bool poll(CloudResponse &response) override;
    ErrorInfo statusError() const override;
    bool liveUpdates() const override { return liveUpdates_.load(); }
    bool remoteLive() const override { return remoteLive_.load(); }
    // true when the requests keep failing although WiFi is up and the recovery did not help (-> restart)
    bool stuck() const { return stuck_.load(); }

    // from the auth / stream callbacks (in the network task)
    void onAuthEvent(int code, const char *message, bool isError);
    void onStreamEvent(const String &event, const String &path, const String &data);
    void onStreamError(int code, const char *message);
    void onRemoteStreamEvent(const String &event, const String &path, const String &data);
    void onRemoteStreamError(int code, const char *message);

private:
    static void taskEntry(void *arg);
    void run();
    void startApp();
    void process(CloudRequest &req);
    void rejectExpiredHead();
    // One SSE connection (the config stream and the remote purchase request stream)
    struct Stream
    {
        const char *name;
        bool active = false;
        bool restart = false;
        uint32_t lastEventMs = 0;
        uint32_t nextStartMs = 0;
    };
    void maintainStreams(bool online, bool ready);
    void maintainStream(Stream &s, bool online, bool ready, bool reconnected);
    void startStream(Stream &s);
    void stopStream(Stream &s, const char *reason);
    bool streamLive(const Stream &s) const;
    bool remoteHeapOk();
    void pollIfDue();
    void respondRemote(const String &id, const String &json);

    void fetchUser(const CloudRequest &req, CloudResponse &resp);
    void updateUser(const CloudRequest &req, CloudResponse &resp);
    void transfer(const CloudRequest &req, CloudResponse &resp);
    bool writeLog(const CloudRequest &req, ErrorInfo &err);
    bool writeDeviceEvent(const CloudRequest &req, ErrorInfo &err);
    void trackWifiEvents(bool online);
    void fetchConfig(const CloudRequest &req, CloudResponse &resp);
    bool writeRemoteStatus(const CloudRequest &req, ErrorInfo &err);
    bool writeRemoteOnline(const CloudRequest &req, ErrorInfo &err);

    bool lastCallOk(const char *op, const char *path, ErrorInfo &out);
    void trackCallResult(bool ok, const ErrorInfo &err);
    void respond(const CloudResponse &resp);
    void setStatusError(const ErrorInfo &e);

    WifiNetwork &wifi_;
    SystemClock &clock_;
    QueueHandle_t requests_ = nullptr;
    QueueHandle_t responses_ = nullptr;
    TaskHandle_t task_ = nullptr;
    INetTaskClient *netClient_ = nullptr;

    std::atomic<bool> ready_{false};
    bool appStarted_ = false;
    bool wasReady_ = false;
    bool wasOnline_ = false;

    // config stream (SSE) on the DB_CONF_PATH path, app purchase requests on DB_REMOTE_PATH<id>/req
    Stream confStream_{"Config"};
    Stream remoteStream_{"Remote request"};
    std::atomic<bool> liveUpdates_{false};
    std::atomic<bool> remoteLive_{false};
    bool remoteHeapWarned_ = false;
    uint32_t remotePausedUntilMs_ = 0;

    // polling mode (settings::kUseStreams == false)
    uint32_t nextConfPollMs_ = 0;
    uint32_t nextRemotePollMs_ = 0;
    uint32_t lastConfPollOkMs_ = 0;
    uint32_t lastRemotePollOkMs_ = 0;
    String lastConfPolled_;

    // request failures in a row (see trackCallResult)
    uint8_t failStreak_ = 0;
    uint32_t lastCallOkMs_ = 0;
    bool forcedReconnect_ = false;
    uint32_t failHeap_ = 0;
    uint32_t failMaxBlock_ = 0;
    std::atomic<bool> stuck_{false};
    uint32_t lastLogKey_ = 0;   // the latest users/<UID>/log key (strictly increasing)
    uint32_t lastEventKey_ = 0; // the latest /Log key

    // WiFi events to /Log
    bool eventOnline_ = false;
    bool everOnline_ = false;
    uint32_t offlineSinceMs_ = 0;

    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    ErrorInfo statusError_;
};
