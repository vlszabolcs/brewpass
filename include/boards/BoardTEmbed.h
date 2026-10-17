#pragma once

#include <cstdint>

// Test hardware: LilyGO T-Embed CC1101 Plus (ESP32-S3)
// Source: https://github.com/Xinyuan-LilyGO/T-Embed-CC1101 (examples/utilities.h)
namespace board
{
    constexpr const char *kShortName = "T-Embed"; // for the device log
    constexpr const char *kName = "Test hardware (T-Embed CC1101 Plus)";

    // Power of the peripherals (display, PN532, speaker, LED) - must be pulled HIGH at startup
    constexpr uint8_t kPowerEnablePin = 15;

    // Display (ST7789 170x320) - the TFT_eSPI setup is in the platformio.ini build_flags
    constexpr uint8_t kDisplayRotation = 3; // landscape, 320x170
    constexpr uint8_t kDisplayBacklightPin = 21;

    // Other devices on the shared SPI bus - their CS pins are kept HIGH so they do not interfere
    constexpr uint8_t kSdCsPin = 13;
    constexpr uint8_t kRadioCsPin = 12; // CC1101
    constexpr uint8_t kNrf24CsPin = 44;

    // I2C (PN532, PMU)
    constexpr uint8_t kI2cSdaPin = 8;
    constexpr uint8_t kI2cSclPin = 18;
    constexpr uint32_t kI2cClockHz = 100000;

    // PN532
    constexpr uint8_t kPn532IrqPin = 17;
    constexpr uint8_t kPn532ResetPin = 45;

    // Buttons (active LOW)
    constexpr uint8_t kEncoderKeyPin = 0; // push button of the rotary encoder
    constexpr uint8_t kUserKeyPin = 6;    // side button

    // WS2812 LED ring
    constexpr uint8_t kLedPin = 14;
    constexpr uint8_t kLedCount = 8;
    constexpr uint8_t kLedBrightness = 40;

    // I2S speaker. Default state of the sound (the user toggles it with a long encoder press on the
    // "Fizess!" screen; the choice is saved in NVS and survives a restart). LED feedback is always on.
    constexpr bool kSoundDefaultOn = true;
    constexpr uint32_t kSoundToggleHoldMs = 1000; // encoder button long press
    constexpr uint8_t kI2sBclkPin = 46;
    constexpr uint8_t kI2sLrclkPin = 40;
    constexpr uint8_t kI2sDoutPin = 7;

    // Coffee machine simulation
    // 0: after a successful credit entry the machine stays busy until taking the cup is simulated with the encoder button.
    // >0: it also becomes ready by itself after this many ms.
    constexpr uint32_t kSimBrewMs = 0;
    constexpr uint32_t kSimLongPressMs = 1000; // side button long press: "the machine ignores the pulses"
    constexpr uint32_t kPowerOffHoldMs = 5000; // side button held this long: power off on battery (power on: PWR button)
} // namespace board
