#pragma once

// Add-on service called periodically in the network task (FirebaseCloud), e.g. OTA.
class INetTaskClient
{
public:
    virtual ~INetTaskClient() = default;
    // online: there is a WiFi connection
    virtual void onNetTick(bool online) = 0;
};
