#pragma once

#include <Interfaces.h>
#include <functional>

#include "../common/Button.h"

// Coffee machine simulation on the test hardware (the T-Embed has no machine connector).
//
// The simulated machine behaves like the Saeco: it accepts credit by default (inhibit LOW); after a successful
// credit entry it is busy (inhibit HIGH) until taking the cup is simulated with the encoder button (-> inhibit LOW).
// If kSimBrewMs > 0 it also becomes ready by itself after that time; 0 = button only.
//
// Buttons:
//   encoder button (short)  - cup taken: the machine goes from busy to ready again (inhibit -> LOW)
//   encoder button (long)   - while brewing: cup taken; otherwise the request goes to the function set with
//                             setEncoderLongPressHandler() (sound on/off on the "Fizess!" screen)
//   side button (short)     - machine busy / fault on/off (inhibit held HIGH)
//   side button (1-5 s)     - on release: "machine ignores the pulses" on/off (VendNotAcknowledged test)
//   side button (5 s)       - power off request - handled by the function set with setPowerOffHandler()
class SimCoinMechanism : public ICoinMechanism
{
public:
    SimCoinMechanism(uint8_t vendDoneButtonPin, uint8_t machineButtonPin, uint32_t brewMs, uint32_t longPressMs,
                     uint32_t encoderLongPressMs, uint32_t powerOffHoldMs, uint8_t pulseCount, uint32_t pulseLowMs,
                     uint32_t pulseHighMs);

    // called when the side button is held for 5 s (from the main loop)
    void setPowerOffHandler(std::function<void()> handler) { powerOffHandler_ = std::move(handler); }
    // long press of the encoder button outside brewing (from the main loop)
    void setEncoderLongPressHandler(std::function<void()> handler) { encoderLongHandler_ = std::move(handler); }

    void begin() override;
    void loop(uint32_t nowMs) override;
    bool acceptingCredit() const override { return !(forcedBusy_ || brewing_); }
    bool startCreditPulses() override;
    bool pulsing() const override { return edgesLeft_ > 0; }
    const char *name() const override { return "Simulated machine (buttons)"; }

private:
    Button vendDoneButton_;
    Button machineButton_;
    std::function<void()> powerOffHandler_;
    std::function<void()> encoderLongHandler_;
    const uint32_t brewMs_;
    const uint8_t pulseCount_;
    const uint32_t pulseLowMs_;
    const uint32_t pulseHighMs_;

    bool forcedBusy_ = false;
    bool ignorePulses_ = false;
    bool brewing_ = false;
    uint32_t brewEndMs_ = 0;

    uint8_t edgesLeft_ = 0;
    bool outLow_ = false;
    uint32_t nextEdgeMs_ = 0;
    uint8_t pulsesSeen_ = 0;
};
