#pragma once

#include <cstdint>

// Target hardware: ESP32 (ESP-WROOM-32) + RC522 (SPI) + SH1106 128x64 OLED (I2C) + passive buzzer
// The pinout is the same as in the original rfidChecker project.
namespace board
{
    constexpr const char *kShortName = "target"; // for the device log
    constexpr const char *kName = "Target hardware (ESP32 + RC522 + SH1106)";

    // Coffee machine interface
    constexpr uint8_t kCoinOutPin = 25; // COIN output (active LOW)
    constexpr uint8_t kInhibitPin = 34; // inhibit input (LOW = machine accepts credit); input only, no internal pull-up
    // true: open-drain output (recommended if the machine pulls the COIN line to 5 V - then 3.3 V is not
    // driven onto the 5 V line in the HIGH state). The original was push-pull, hence the default is false.
    constexpr bool kCoinOutOpenDrain = false;

    // Buzzer
    constexpr uint8_t kBuzzerPin = 27;
    constexpr uint8_t kBuzzerLedcChannel = 0;

    // RC522 (VSPI: SCK 18, MISO 19, MOSI 23)
    // Note: GPIO2 is a strapping pin - if there are boot problems, move SS to another pin.
    constexpr uint8_t kRc522SsPin = 2;
    constexpr uint8_t kRc522RstPin = 4;

    // OLED: hardware I2C on the default pins (SDA 21, SCL 22)
    constexpr uint32_t kOledI2cClockHz = 400000;

    // OLED burn-in protection
    // Contrast (0..255, the SH1106 default is 128): a lower value slows the burn-in down
    constexpr uint8_t kOledContrast = 64;
    constexpr uint8_t kOledSaverContrast = 8;
    // Screen saver after this long without activity (card, app request, new screen, machine state change);
    // 0 disables it. Its text jumps to a new place every kOledSaverMoveMs.
    constexpr uint32_t kOledSaverIdleMs = 0; // switched off for now (to be revisited; was 60000)
    constexpr uint32_t kOledSaverMoveMs = 8000;
    // Pixel shift of the normal screen: one pixel step this often, within +-kOledShiftMaxX / +-kOledShiftMaxY
    constexpr uint32_t kOledShiftIntervalMs = 60000;
    constexpr uint8_t kOledShiftMaxX = 2;
    constexpr uint8_t kOledShiftMaxY = 1;
} // namespace board
