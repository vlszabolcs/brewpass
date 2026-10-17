// Test hardware assembly: LilyGO T-Embed CC1101 Plus
// PN532 (I2C) + ST7789 TFT + I2S speaker/WS2812 + coffee machine simulated with buttons
#include <Arduino.h>
#include <Log.h>
#include <Messages.h>

#include "../common/Hardware.h"
#include "BoardConfig.h"
#include "Pn532Reader.h"
#include "Settings.h"
#include "SimCoinMechanism.h"
#include "TEmbedFeedback.h"
#include "TEmbedPower.h"
#include "TftUi.h"

namespace board
{
    namespace
    {
        TEmbedPower &power()
        {
            static TEmbedPower instance(kI2cSdaPin, kI2cSclPin);
            return instance;
        }

        // Side button held for 5 s: real power off on battery (the charger disconnects the battery,
        // power on with the PWR button). With USB power there is nothing to do - only a short notice.
        void powerOff(TftUi &ui, TEmbedFeedback &feedback)
        {
            if (!power().begin())
            {
                ui.showNotice(msg::kPowerOffFailedTitle, msg::kPowerOffNoCharger, 3000);
                feedback.play(FeedbackPattern::Error);
                return;
            }
            if (power().externalPower())
            {
                LOGI("POWER", "USB power present - power off not needed");
                ui.showNotice(msg::kPowerOffUsbTitle, msg::kPowerOffUsbHint, 2500);
                return;
            }

            LOGW("POWER", "Powering off (battery)");
            ui.showNotice(msg::kPowerOffTitle, msg::kPowerOffHint, 0);
            feedback.allOff();
            delay(1500); // time to read the notice

            power().shutdown();
            delay(3000);

            // still running: the power was not cut (e.g. USB was connected meanwhile)
            LOGE("POWER", "Power off failed - the device keeps running");
            ui.showNotice(msg::kPowerOffFailedTitle, msg::kPowerOffFailedHint, 4000);
        }
    } // namespace

    void earlyInit()
    {
        // peripheral power
        pinMode(kPowerEnablePin, OUTPUT);
        digitalWrite(kPowerEnablePin, HIGH);

        // keep the other devices on the shared SPI bus deselected
        for (uint8_t cs : {kSdCsPin, kRadioCsPin, kNrf24CsPin})
        {
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
        }
        delay(50); // let the supply settle (PN532, display)
    }

    Hardware &hardware()
    {
        static Pn532Reader reader(kPn532IrqPin, kPn532ResetPin, kI2cSdaPin, kI2cSclPin, kI2cClockHz);
        static SimCoinMechanism coin(kEncoderKeyPin, kUserKeyPin, kSimBrewMs, kSimLongPressMs, kPowerOffHoldMs,
                                     settings::kCoinPulseCount, settings::kCoinPulseLowMs, settings::kCoinPulseHighMs);
        static TftUi ui(kDisplayRotation);
        static TEmbedFeedback feedback(kI2sBclkPin, kI2sLrclkPin, kI2sDoutPin, kLedPin, kLedCount, kLedBrightness,
                                       kSoundEnabled);
        static Hardware hw{reader, coin, ui, feedback};
        static bool wired = false;
        if (!wired)
        {
            wired = true;
            coin.setPowerOffHandler([]() { powerOff(ui, feedback); });
        }
        return hw;
    }
} // namespace board
