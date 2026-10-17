#pragma once

#include <cstdint>

// Power management of the T-Embed CC1101 through its BQ25896 charger (I2C 0x6B, XPowersLib).
//
// Real power off = "ship mode": the charger disconnects the battery (BATFET off). After that the board
// can only be started again with the PWR button or by connecting USB power. This only works on battery:
// with USB power present the board keeps running from VBUS, so power off is refused in that case.
class TEmbedPower
{
public:
    TEmbedPower(uint8_t sdaPin, uint8_t sclPin);

    // Lazy init of the charger (the shared I2C bus is started by the PN532 reader). Safe to call repeatedly.
    bool begin();
    bool ready() const { return ready_; }

    // true if USB / external power is present (VBUS good or a non-OTG input source detected)
    bool externalPower();

    // Disconnects the battery. Only returns if the power was NOT cut (e.g. USB power appeared meanwhile).
    void shutdown();

private:
    uint8_t sdaPin_;
    uint8_t sclPin_;
    bool ready_ = false;
};
