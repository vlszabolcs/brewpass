#pragma once

#include <Adafruit_PN532.h>
#include <Interfaces.h>

// PN532 NFC reader (I2C, with IRQ pin) in non-blocking mode.
//
// Initialisation (reset, firmware, SAM) is done by the Adafruit library, but card detection uses our own
// frame handling: over I2C, Adafruit waits by polling the "ready" byte while the PN532 is busy and holds
// the bus -> one-second I2C timeouts and a blocked main loop. Here the bus is only read when the PN532
// signals pending data on the IRQ pin (same method as the LilyGO example).
//
//  - InListPassiveTarget is armed with finite retries -> the PN532 answers without a card too (heartbeat)
//  - IRQ LOW -> read the response (a card or "no card"), then re-arm
//  - if the heartbeat stops, the reader is reset in hardware
class Pn532Reader : public ICardReader
{
public:
    Pn532Reader(uint8_t irqPin, uint8_t resetPin, uint8_t sdaPin, uint8_t sclPin, uint32_t i2cClockHz);

    bool begin() override;
    bool poll(CardUid &out) override;
    bool healthy() const override { return healthy_; }
    const ErrorInfo &lastError() const override { return error_; }
    const char *name() const override { return "PN532"; }

private:
    bool init();
    bool arm(uint32_t nowMs);
    bool readDetection(CardUid &out);
    void markFailed(const char *what, int32_t code);

    // low-level I2C frame handling
    bool writeCommand(const uint8_t *cmd, uint8_t len);
    bool waitIrq(uint32_t timeoutMs);
    bool readAck();
    // command + ACK + reading the response (the response content is not returned)
    bool transceive(const uint8_t *cmd, uint8_t len);
    size_t readFrame(uint8_t *buf, size_t len);

    Adafruit_PN532 nfc_;
    uint8_t irqPin_;
    uint8_t resetPin_;
    uint8_t sdaPin_;
    uint8_t sclPin_;
    uint32_t i2cClockHz_;

    bool healthy_ = false;
    bool armed_ = false;
    ErrorInfo error_;
    uint32_t armedAtMs_ = 0;
    uint32_t nextArmMs_ = 0;
    uint32_t nextRecoverMs_ = 0;
    uint32_t recoveries_ = 0;
};
