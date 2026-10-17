#include "TEmbedPower.h"

#define XPOWERS_CHIP_BQ25896
#include <Log.h>
#include <Wire.h>
#include <XPowersLib.h>

namespace
{
    constexpr const char *TAG = "POWER";
    XPowersPPM pmu;
} // namespace

TEmbedPower::TEmbedPower(uint8_t sdaPin, uint8_t sclPin) : sdaPin_(sdaPin), sclPin_(sclPin) {}

bool TEmbedPower::begin()
{
    if (ready_)
        return true;
    ready_ = pmu.init(Wire, sdaPin_, sclPin_, BQ25896_SLAVE_ADDRESS);
    if (ready_)
        LOGI(TAG, "Charger ready: %s", pmu.getChipName());
    else
        LOGE(TAG, "Charger (BQ25896, I2C 0x%02X) not responding", BQ25896_SLAVE_ADDRESS);
    return ready_;
}

bool TEmbedPower::externalPower()
{
    if (!begin())
        return false;
    const auto bus = pmu.getBusStatus();
    const bool inputSource = bus != XPowersPPM::BUS_STATE_NOINPUT && bus != XPowersPPM::BUS_STATE_OTG;
    return pmu.isVbusIn() || inputSource;
}

void TEmbedPower::shutdown()
{
    if (!begin())
        return;
    LOGW(TAG, "Disconnecting the battery (ship mode) - power on with the PWR button");
    Serial.flush();
    pmu.shutdown();
}
