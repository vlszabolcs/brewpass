#include "Mfrc522Reader.h"

#include <Log.h>
#include <SPI.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "RC522";
}

Mfrc522Reader::Mfrc522Reader(uint8_t ssPin, uint8_t rstPin) : rfid_(ssPin, rstPin) {}

bool Mfrc522Reader::checkVersion(uint8_t &version)
{
    version = rfid_.PCD_ReadRegister(MFRC522::VersionReg);
    // 0x00 / 0xFF: no communication (wiring, power, hang)
    return version != 0x00 && version != 0xFF;
}

bool Mfrc522Reader::begin()
{
    SPI.begin();
    rfid_.PCD_Init();
    lastCheckMs_ = millis();

    uint8_t version = 0;
    healthy_ = checkVersion(version);
    if (healthy_)
    {
        error_ = ErrorInfo::none();
        LOGI(TAG, "Reader ready, firmware: 0x%02X", version);
    }
    else
    {
        error_ = ErrorInfo::make(ErrorCode::ReaderFault, version, "RC522 not responding (VersionReg=0x%02X). Check the wiring!",
                                 version);
        LOGE(TAG, "%s", error_.detail);
    }
    return healthy_;
}

void Mfrc522Reader::recover()
{
    recoveries_++;
    LOGW(TAG, "Re-initialising #%lu", (unsigned long)recoveries_);
    rfid_.PCD_Init();
    uint8_t version = 0;
    healthy_ = checkVersion(version);
    if (healthy_)
    {
        error_ = ErrorInfo::none();
        LOGI(TAG, "Reader recovered (0x%02X)", version);
    }
    else
    {
        error_ = ErrorInfo::make(ErrorCode::ReaderFault, version,
                                 "RC522 not responding (VersionReg=0x%02X), recovery attempt %lu", version,
                                 (unsigned long)recoveries_);
    }
}

bool Mfrc522Reader::poll(CardUid &out)
{
    const uint32_t now = millis();
    const uint32_t interval = healthy_ ? settings::kReaderHealthCheckMs : settings::kReaderRecoverRetryMs;
    if (now - lastCheckMs_ >= interval)
    {
        lastCheckMs_ = now;
        uint8_t version = 0;
        if (!checkVersion(version))
        {
            if (healthy_)
                LOGE(TAG, "Reader not responding (VersionReg=0x%02X)", version);
            healthy_ = false;
            recover();
        }
    }
    if (!healthy_)
        return false;

    if (!rfid_.PICC_IsNewCardPresent())
        return false;
    if (!rfid_.PICC_ReadCardSerial())
    {
        LOGW(TAG, "Card detected, but reading the UID failed");
        return false;
    }

    out = CardUid();
    out.length = rfid_.uid.size <= CardUid::kMaxLength ? rfid_.uid.size : CardUid::kMaxLength;
    memcpy(out.bytes, rfid_.uid.uidByte, out.length);

    rfid_.PICC_HaltA();
    rfid_.PCD_StopCrypto1();
    return true;
}
