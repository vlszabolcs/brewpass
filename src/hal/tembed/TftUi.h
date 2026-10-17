#pragma once

#include <Interfaces.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

// ST7789 320x170 colour UI (TFT_eSPI + U8g2 fonts for the Hungarian accents).
// The full frame is drawn into a sprite (PSRAM) and pushed to the display without flicker.
class TftUi : public IUi
{
public:
    explicit TftUi(uint8_t rotation);

    bool begin() override;
    void show(const Screen &screen) override;
    void setStatus(const StatusInfo &status) override;
    void loop(uint32_t nowMs) override;

    // Short notice drawn over the current screen for durationMs (0 = until replaced by another notice);
    // the underlying screen is kept and comes back when the notice expires. Draws immediately.
    void showNotice(const char *title, const char *text, uint32_t durationMs);

private:
    void render();
    void drawStatusBar();
    void drawBody();
    void drawError();
    void drawNotice();
    void text(const uint8_t *font, uint16_t color, int16_t x, int16_t y, const char *s);
    void centered(const uint8_t *font, uint16_t color, int16_t y, const char *s);
    int wrapped(const uint8_t *font, uint16_t color, int16_t x, int16_t y, int16_t w, int16_t lineH, const char *s,
                int maxLines, bool center = false);

    TFT_eSPI tft_;
    TFT_eSprite sprite_;
    TFT_eSPI *canvas_ = nullptr;
    U8g2_for_TFT_eSPI fonts_;
    uint8_t rotation_;
    bool useSprite_ = false;

    Screen screen_;
    StatusInfo status_;
    bool dirty_ = true;
    uint32_t lastRenderMs_ = 0;

    bool noticeActive_ = false;
    uint32_t noticeUntilMs_ = 0; // 0 = no expiry
    char noticeTitle_[48] = {0};
    char noticeText_[96] = {0};
};
