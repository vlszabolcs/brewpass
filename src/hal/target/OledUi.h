#pragma once

#include <Interfaces.h>
#include <ScreenSaver.h>
#include <U8g2lib.h>

// SH1106 128x64 OLED UI (U8g2, full frame buffer, UTF-8 with Hungarian accents).
// Burn-in protection: reduced contrast, pixel shift of the normal screen and a moving screen saver when idle.
class OledUi : public IUi
{
public:
    OledUi(uint32_t i2cClockHz, const ScreenSaverConfig &saver, uint8_t contrast, uint8_t saverContrast);

    bool begin() override;
    void show(const Screen &screen) override;
    void setStatus(const StatusInfo &status) override;
    void loop(uint32_t nowMs) override;

private:
    void render();
    void drawStatusBar();
    void drawBody();
    void drawError();
    void drawSaver(uint32_t nowMs);
    void setContrast(uint8_t value);

    U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2_;
    uint32_t i2cClockHz_;
    Screen screen_;
    StatusInfo status_;
    bool dirty_ = true;
    uint32_t lastRenderMs_ = 0;
    // on the error screen the details and the hint are shown alternately
    bool showHint_ = false;
    bool saverArmed_ = false; // the first purchase switches the screen saver on
    uint32_t altToggleMs_ = 0;
    ScreenSaver saver_;
    uint8_t contrast_;
    uint8_t saverContrast_;
    int contrastSent_ = -1;
};
