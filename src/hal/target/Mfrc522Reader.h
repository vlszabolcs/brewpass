#pragma once

#include <Interfaces.h>
#include <MFRC522.h>

// RC522 reader (SPI). Checks VersionReg periodically; if the reader hung (0x00 / 0xFF)
// it is re-initialised - a known weakness of the RC522 in the original project.
class Mfrc522Reader : public ICardReader
{
public:
    Mfrc522Reader(uint8_t ssPin, uint8_t rstPin);

    bool begin() override;
    bool poll(CardUid &out) override;
    bool healthy() const override { return healthy_; }
    const ErrorInfo &lastError() const override { return error_; }
    const char *name() const override { return "RC522"; }

private:
    bool checkVersion(uint8_t &version);
    void recover();

    MFRC522 rfid_;
    bool healthy_ = false;
    ErrorInfo error_;
    uint32_t lastCheckMs_ = 0;
    uint32_t recoveries_ = 0;
};
