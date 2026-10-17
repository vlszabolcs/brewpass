// Target hardware assembly: ESP32 + RC522 + SH1106 OLED + buzzer + real coffee machine interface
#include <Arduino.h>

#include "../common/GpioCoinMechanism.h"
#include "../common/Hardware.h"
#include "BoardConfig.h"
#include "BuzzerFeedback.h"
#include "Mfrc522Reader.h"
#include "OledUi.h"
#include "Settings.h"

namespace board
{
    void earlyInit()
    {
        // COIN output inactive (HIGH) immediately - before any other peripheral starts
        digitalWrite(kCoinOutPin, HIGH);
        pinMode(kCoinOutPin, kCoinOutOpenDrain ? OUTPUT_OPEN_DRAIN : OUTPUT);
        digitalWrite(kCoinOutPin, HIGH);
    }

    Hardware &hardware()
    {
        static Mfrc522Reader reader(kRc522SsPin, kRc522RstPin);
        static GpioCoinMechanism coin(kCoinOutPin, kInhibitPin, kCoinOutOpenDrain,
                                      {settings::kCoinPulseCount, settings::kCoinPulseLowMs, settings::kCoinPulseHighMs},
                                      settings::kInhibitDebounceMs);
        static OledUi ui(kOledI2cClockHz);
        static BuzzerFeedback feedback(kBuzzerPin, kBuzzerLedcChannel);
        static Hardware hw{reader, coin, ui, feedback};
        return hw;
    }
} // namespace board
