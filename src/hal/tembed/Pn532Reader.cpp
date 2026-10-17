#include "Pn532Reader.h"

#include <Log.h>
#include <Wire.h>

#include "Settings.h"

namespace
{
    constexpr const char *TAG = "PN532";
    constexpr uint8_t kI2cAddress = 0x24;
    constexpr uint8_t kRdyByte = 0x01;
    constexpr uint8_t kHostToPn532 = 0xD4;
    constexpr uint8_t kPn532ToHost = 0xD5;
    constexpr uint8_t kCmdInListPassiveTarget = 0x4A;
    constexpr uint8_t kCmdRfConfiguration = 0x32;
    constexpr uint32_t kCommandResponseTimeoutMs = 200;

    // Finite retries -> the PN532 also answers when there is no card (heartbeat)
    constexpr uint8_t kPassiveRetries = 0x05;
    // A response must arrive within this time after arming, otherwise the reader has hung
    constexpr uint32_t kResponseTimeoutMs = 3000;
    constexpr uint32_t kAckTimeoutMs = 30;
    constexpr uint32_t kRearmDelayMs = 50;
    constexpr uint16_t kWireTimeoutMs = 25;
} // namespace

Pn532Reader::Pn532Reader(uint8_t irqPin, uint8_t resetPin, uint8_t sdaPin, uint8_t sclPin, uint32_t i2cClockHz)
    : nfc_(irqPin, resetPin, &Wire), irqPin_(irqPin), resetPin_(resetPin), sdaPin_(sdaPin), sclPin_(sclPin),
      i2cClockHz_(i2cClockHz)
{
}

bool Pn532Reader::begin()
{
    pinMode(irqPin_, INPUT_PULLUP);
    pinMode(resetPin_, OUTPUT);
    digitalWrite(resetPin_, HIGH);
    Wire.begin(sdaPin_, sclPin_, i2cClockHz_);
    return init();
}

bool Pn532Reader::init()
{
    armed_ = false;
    nfc_.begin(); // hardware reset + wake-up
    Wire.setTimeOut(kWireTimeoutMs);

    const uint32_t version = nfc_.getFirmwareVersion();
    if (version == 0)
    {
        markFailed("not responding on the I2C bus (0x24)", 0);
        return false;
    }
    if (!nfc_.SAMConfig())
    {
        markFailed("SAMConfig sikertelen", 0);
        return false;
    }
    // Adafruit's setPassiveActivationRetries() does not read the response, after which the PN532 accepts
    // no new command - so it is set with our own complete (command + ACK + response) transaction.
    static constexpr uint8_t kRetriesCmd[] = {kCmdRfConfiguration, 0x05 /* MaxRetries */, 0xFF, 0x01, kPassiveRetries};
    if (!transceive(kRetriesCmd, sizeof(kRetriesCmd)))
    {
        markFailed("setting the retry count failed", 0);
        return false;
    }

    healthy_ = true;
    error_ = ErrorInfo::none();
    LOGI(TAG, "Reader ready: PN5%02X, firmware %u.%u", (unsigned)((version >> 24) & 0xFF),
         (unsigned)((version >> 16) & 0xFF), (unsigned)((version >> 8) & 0xFF));
    nextArmMs_ = millis();
    return true;
}

void Pn532Reader::markFailed(const char *what, int32_t code)
{
    if (healthy_ || recoveries_ == 0)
        LOGE(TAG, "Reader fault: %s", what);
    healthy_ = false;
    armed_ = false;
    error_ = ErrorInfo::make(ErrorCode::ReaderFault, code, "PN532: %s (recovery %lu)", what, (unsigned long)recoveries_);
    nextRecoverMs_ = millis() + settings::kReaderRecoverRetryMs;
}

// ------------------------------------------------------------------ I2C keretek

bool Pn532Reader::writeCommand(const uint8_t *cmd, uint8_t len)
{
    // 00 00 FF LEN LCS D4 CMD... DCS 00
    uint8_t frame[32];
    if (len + 8u > sizeof(frame))
        return false;
    const uint8_t frameLen = len + 1;
    uint8_t sum = kHostToPn532;
    size_t n = 0;
    frame[n++] = 0x00;
    frame[n++] = 0x00;
    frame[n++] = 0xFF;
    frame[n++] = frameLen;
    frame[n++] = static_cast<uint8_t>(~frameLen + 1);
    frame[n++] = kHostToPn532;
    for (uint8_t i = 0; i < len; i++)
    {
        frame[n++] = cmd[i];
        sum += cmd[i];
    }
    frame[n++] = static_cast<uint8_t>(~sum + 1);
    frame[n++] = 0x00;

    Wire.beginTransmission(kI2cAddress);
    Wire.write(frame, n);
    return Wire.endTransmission() == 0;
}

bool Pn532Reader::waitIrq(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    while (digitalRead(irqPin_) != LOW)
    {
        if (millis() - start > timeoutMs)
            return false;
        delay(1);
    }
    return true;
}

size_t Pn532Reader::readFrame(uint8_t *buf, size_t len)
{
    // the first byte is RDY (0x01), it is dropped
    const size_t got = Wire.requestFrom(kI2cAddress, static_cast<uint8_t>(len + 1));
    if (got == 0)
        return 0;
    const int rdy = Wire.read();
    size_t n = 0;
    while (Wire.available() && n < len)
        buf[n++] = static_cast<uint8_t>(Wire.read());
    return rdy == kRdyByte ? n : 0;
}

bool Pn532Reader::readAck()
{
    static constexpr uint8_t kAck[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t buf[sizeof(kAck)] = {0};
    return readFrame(buf, sizeof(buf)) == sizeof(buf) && memcmp(buf, kAck, sizeof(kAck)) == 0;
}

bool Pn532Reader::transceive(const uint8_t *cmd, uint8_t len)
{
    if (!writeCommand(cmd, len) || !waitIrq(kAckTimeoutMs) || !readAck())
        return false;
    if (!waitIrq(kCommandResponseTimeoutMs))
        return false;
    uint8_t buf[16];
    const size_t n = readFrame(buf, sizeof(buf));
    // response: 00 00 FF LEN LCS D5 (CMD+1) ...
    return n >= 7 && buf[5] == kPn532ToHost && buf[6] == static_cast<uint8_t>(cmd[0] + 1);
}

// ------------------------------------------------------------------ card detection

bool Pn532Reader::arm(uint32_t nowMs)
{
    static constexpr uint8_t kCmd[] = {kCmdInListPassiveTarget, 0x01 /* max 1 card */, PN532_MIFARE_ISO14443A};
    if (!writeCommand(kCmd, sizeof(kCmd)))
    {
        markFailed("sending the command failed (I2C)", 0);
        return false;
    }
    if (!waitIrq(kAckTimeoutMs) || !readAck())
    {
        markFailed("no ACK for the card detection command", 0);
        return false;
    }
    armed_ = true;
    armedAtMs_ = nowMs;
    return true;
}

bool Pn532Reader::readDetection(CardUid &out)
{
    // 00 00 FF LEN LCS D5 4B NbTg Tg SENS_RES(2) SEL_RES NFCIDLen NFCID... DCS 00
    uint8_t buf[32] = {0};
    const size_t n = readFrame(buf, sizeof(buf));
    if (n < 8 || buf[0] != 0x00 || buf[1] != 0x00 || buf[2] != 0xFF)
    {
        LOGW(TAG, "Invalid response frame (%u bytes)", (unsigned)n);
        return false;
    }
    if (buf[5] != kPn532ToHost || buf[6] != kCmdInListPassiveTarget + 1)
    {
        LOGW(TAG, "Unexpected response: %02X %02X", buf[5], buf[6]);
        return false;
    }
    if (buf[7] == 0)
        return false; // no card - normal heartbeat

    const uint8_t uidLen = buf[12];
    if (uidLen == 0 || uidLen > CardUid::kMaxLength || 13u + uidLen > n)
    {
        LOGW(TAG, "Invalid UID length: %u", uidLen);
        return false;
    }
    out = CardUid();
    out.length = uidLen;
    memcpy(out.bytes, &buf[13], uidLen);
    return true;
}

bool Pn532Reader::poll(CardUid &out)
{
    const uint32_t now = millis();

    if (!healthy_)
    {
        if (static_cast<int32_t>(now - nextRecoverMs_) >= 0)
        {
            recoveries_++;
            LOGW(TAG, "Re-initialising #%lu", (unsigned long)recoveries_);
            init();
        }
        return false;
    }

    if (!armed_)
    {
        if (static_cast<int32_t>(now - nextArmMs_) >= 0)
            arm(now);
        return false;
    }

    if (digitalRead(irqPin_) != LOW)
    {
        if (now - armedAtMs_ > kResponseTimeoutMs)
            markFailed("no response (IRQ) - hung?", 0);
        return false;
    }

    // a response arrived: a card or "no card"
    armed_ = false;
    nextArmMs_ = now + kRearmDelayMs;
    return readDetection(out);
}
