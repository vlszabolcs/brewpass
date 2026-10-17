#include "TftUi.h"

#include <Log.h>
#include <Messages.h>

#include "../common/TextWrap.h"

namespace
{
    constexpr const char *TAG = "TFT";
    constexpr int16_t kW = 320;
    constexpr int16_t kH = 170;
    constexpr int16_t kBarH = 24;
    constexpr int16_t kMargin = 10;
    constexpr uint32_t kMinRenderIntervalMs = 100;

    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // Colours
    constexpr uint16_t kBarBg = rgb(30, 35, 41);
    constexpr uint16_t kBarText = rgb(200, 205, 211);
    constexpr uint16_t kWhite = rgb(255, 255, 255);
    constexpr uint16_t kGrey = rgb(150, 156, 163);
    constexpr uint16_t kGreen = rgb(46, 204, 113);
    constexpr uint16_t kRed = rgb(231, 76, 60);
    constexpr uint16_t kAmber = rgb(241, 196, 15);
    constexpr uint16_t kBgIdle = rgb(11, 31, 51);
    constexpr uint16_t kBgFree = rgb(42, 20, 64);
    constexpr uint16_t kBgCheck = rgb(48, 40, 8);
    constexpr uint16_t kBgSuccess = rgb(13, 46, 26);
    constexpr uint16_t kBgError = rgb(58, 14, 14);
    constexpr uint16_t kBgBoot = rgb(26, 29, 33);

    // Fonts (UTF-8, Hungarian accents: _te variants)
    const uint8_t *const kFontBar = u8g2_font_helvB10_te;
    const uint8_t *const kFontSmall = u8g2_font_helvR10_te;
    const uint8_t *const kFontText = u8g2_font_helvR14_te;
    const uint8_t *const kFontTitle = u8g2_font_helvB18_te;
    const uint8_t *const kFontMedium = u8g2_font_helvB14_te;
    const uint8_t *const kFontHuge = u8g2_font_helvB24_te;
    const uint8_t *const kFontAmount = u8g2_font_logisoso42_tn;
    const uint8_t *const kFontIcon = u8g2_font_open_iconic_check_4x_t;
} // namespace

TftUi::TftUi(uint8_t rotation) : sprite_(&tft_), rotation_(rotation) {}

bool TftUi::begin()
{
    tft_.init();
    tft_.setRotation(rotation_);
    tft_.fillScreen(TFT_BLACK);

    sprite_.setColorDepth(16);
    useSprite_ = sprite_.createSprite(kW, kH) != nullptr;
    if (!useSprite_)
    {
        sprite_.setColorDepth(8);
        useSprite_ = sprite_.createSprite(kW, kH) != nullptr;
    }
    if (useSprite_)
    {
        canvas_ = &sprite_;
    }
    else
    {
        LOGW(TAG, "Sprite allocation failed - drawing directly (may flicker)");
        canvas_ = &tft_;
    }
    fonts_.begin(*canvas_);
    fonts_.setFontMode(1);
    fonts_.setFontDirection(0);
    dirty_ = true;
    render();
    return true;
}

void TftUi::show(const Screen &screen)
{
    screen_ = screen;
    dirty_ = true;
}

void TftUi::setStatus(const StatusInfo &status)
{
    if (status != status_)
    {
        status_ = status;
        dirty_ = true;
    }
}

void TftUi::showNotice(const char *title, const char *text, uint32_t durationMs)
{
    snprintf(noticeTitle_, sizeof(noticeTitle_), "%s", title);
    snprintf(noticeText_, sizeof(noticeText_), "%s", text);
    noticeActive_ = true;
    noticeUntilMs_ = durationMs > 0 ? millis() + durationMs : 0;
    if (noticeUntilMs_ == 0 && durationMs > 0)
        noticeUntilMs_ = 1; // avoid the "no expiry" marker on millis() wrap
    lastRenderMs_ = millis();
    render();
}

void TftUi::loop(uint32_t nowMs)
{
    if (noticeActive_ && noticeUntilMs_ != 0 && static_cast<int32_t>(nowMs - noticeUntilMs_) >= 0)
    {
        noticeActive_ = false;
        dirty_ = true;
    }
    if (dirty_ && nowMs - lastRenderMs_ >= kMinRenderIntervalMs)
    {
        lastRenderMs_ = nowMs;
        render();
    }
}

// ------------------------------------------------------------------ helpers

void TftUi::text(const uint8_t *font, uint16_t color, int16_t x, int16_t y, const char *s)
{
    fonts_.setFont(font);
    fonts_.setForegroundColor(color);
    fonts_.drawUTF8(x, y, s);
}

void TftUi::centered(const uint8_t *font, uint16_t color, int16_t y, const char *s)
{
    fonts_.setFont(font);
    fonts_.setForegroundColor(color);
    textwrap::drawCentered(fonts_, 0, y, kW, s);
}

int TftUi::wrapped(const uint8_t *font, uint16_t color, int16_t x, int16_t y, int16_t w, int16_t lineH, const char *s,
                   int maxLines, bool center)
{
    fonts_.setFont(font);
    fonts_.setForegroundColor(color);
    return textwrap::drawWrapped(fonts_, x, y, w, lineH, s, maxLines, center);
}

// ------------------------------------------------------------------ drawing

void TftUi::render()
{
    dirty_ = false;

    uint16_t bg = kBgBoot;
    switch (screen_.kind)
    {
    case ScreenKind::Idle: bg = kBgIdle; break;
    case ScreenKind::FreeIdle:
    case ScreenKind::FreeVend: bg = kBgFree; break;
    case ScreenKind::Checking: bg = kBgCheck; break;
    case ScreenKind::Success: bg = kBgSuccess; break;
    case ScreenKind::Error: bg = kBgError; break;
    case ScreenKind::Boot: bg = kBgBoot; break;
    }
    canvas_->fillRect(0, 0, kW, kH, bg);
    drawStatusBar();
    if (screen_.kind == ScreenKind::Error)
        drawError();
    else
        drawBody();

    if (noticeActive_)
        drawNotice();

    if (useSprite_)
        sprite_.pushSprite(0, 0);
}

void TftUi::drawStatusBar()
{
    canvas_->fillRect(0, 0, kW, kBarH, kBarBg);
    char buf[48];

    // bal: WiFi
    if (status_.wifi)
    {
        snprintf(buf, sizeof(buf), "%s  %d dBm", status_.ssid, status_.rssi);
        fonts_.setFont(kFontBar);
        fonts_.setForegroundColor(kBarText);
        textwrap::drawWrapped(fonts_, 6, 17, 130, 14, buf, 1);
    }
    else
    {
        text(kFontBar, kRed, 6, 17, msg::kNoWifi);
    }

    // centre: cloud + machine state
    canvas_->fillCircle(150, 12, 4, !status_.cloud ? kRed : (status_.liveConfig ? kGreen : kAmber));
    text(kFontBar, kBarText, 158, 17, "DB");
    canvas_->fillCircle(190, 12, 4, status_.machineReady ? kGreen : kAmber);
    text(kFontBar, kBarText, 198, 17, status_.machineReady ? msg::kStateReady : msg::kStateBusy);

    // right: clock
    if (status_.clock[0])
    {
        fonts_.setFont(kFontBar);
        const int16_t w = fonts_.getUTF8Width(status_.clock);
        text(kFontBar, kWhite, kW - 6 - w, 17, status_.clock);
    }
}

void TftUi::drawBody()
{
    char buf[48];
    const int16_t w = kW - 2 * kMargin;

    // device IP in the bottom-left corner of the waiting screens (handy for OTA)
    if (status_.ip[0] &&
        (screen_.kind == ScreenKind::Idle || screen_.kind == ScreenKind::FreeIdle || screen_.kind == ScreenKind::Boot))
    {
        snprintf(buf, sizeof(buf), "IP: %s", status_.ip);
        text(kFontSmall, kGrey, 4, kH - 3, buf);
    }

    switch (screen_.kind)
    {
    case ScreenKind::Idle:
        centered(kFontHuge, kWhite, 82, screen_.title);
        snprintf(buf, sizeof(buf), "%s %ld %s", msg::kPriceLabel, (long)screen_.amount, msg::kCurrency);
        centered(kFontMedium, kAmber, 114, buf);
        wrapped(kFontSmall, kGrey, kMargin, 152, w, 14, status_.machineReady ? screen_.text : msg::kMachineBusy, 1, true);
        break;

    case ScreenKind::FreeIdle:
        centered(kFontHuge, kWhite, 82, screen_.title);
        wrapped(kFontText, kGrey, kMargin, 120, w, 18, status_.machineReady ? screen_.text : msg::kMachineBusy, 2, true);
        break;

    case ScreenKind::Checking:
        fonts_.setFont(kFontIcon);
        fonts_.setForegroundColor(kAmber);
        fonts_.drawGlyph(kMargin + 4, 100, 'C');
        text(kFontTitle, kWhite, 64, 78, screen_.title);
        wrapped(kFontText, kBarText, 64, 108, kW - 64 - kMargin, 18, screen_.text, 2);
        break;

    case ScreenKind::Success:
    {
        wrapped(kFontMedium, kWhite, kMargin, 52, w, 18, screen_.title, 1, true);
        // amount: big digits + "Ft"
        snprintf(buf, sizeof(buf), "%ld", (long)screen_.amount);
        fonts_.setFont(kFontAmount);
        const int16_t numW = fonts_.getUTF8Width(buf);
        fonts_.setFont(kFontTitle);
        const int16_t unitW = fonts_.getUTF8Width(msg::kCurrency);
        const int16_t x = (kW - (numW + 8 + unitW)) / 2;
        text(kFontAmount, screen_.amount < 0 ? kAmber : kGreen, x, 112, buf);
        text(kFontTitle, kWhite, x + numW + 8, 112, msg::kCurrency);
        wrapped(kFontSmall, kBarText, kMargin, 150, w, 14, screen_.text, 1, true);
        break;
    }

    case ScreenKind::FreeVend:
        centered(kFontHuge, kWhite, 82, screen_.title);
        wrapped(kFontText, kGreen, kMargin, 120, w, 18, screen_.text, 2, true);
        break;

    case ScreenKind::Boot:
    case ScreenKind::Error:
    default:
    {
        const bool err = screen_.error.isError();
        const int lines = wrapped(kFontTitle, err ? kAmber : kWhite, kMargin, 56, w, 24, screen_.title, 2);
        int16_t y = 56 + lines * 24 + 4;
        const int textLines = wrapped(kFontText, kBarText, kMargin, y, w, 18, screen_.text, lines > 1 ? 1 : 2);
        y += textLines * 18;
        // technical detail (English) in small grey below the Hungarian hint
        if (err && screen_.error.detail[0])
            wrapped(kFontSmall, kGrey, kMargin, y + 2, w - 40, 14, screen_.error.detail, 2);
        if (err)
        {
            snprintf(buf, sizeof(buf), "E%u", static_cast<unsigned>(screen_.error.code));
            fonts_.setFont(kFontSmall);
            const int16_t cw = fonts_.getUTF8Width(buf);
            text(kFontSmall, kGrey, kW - kMargin - cw, kH - 6, buf);
        }
        break;
    }
    }
}

void TftUi::drawNotice()
{
    constexpr int16_t kBoxX = 16;
    constexpr int16_t kBoxY = kBarH + 14;
    constexpr int16_t kBoxW = kW - 2 * kBoxX;
    constexpr int16_t kBoxH = kH - kBoxY - 12;
    canvas_->fillRoundRect(kBoxX, kBoxY, kBoxW, kBoxH, 8, kBarBg);
    canvas_->drawRoundRect(kBoxX, kBoxY, kBoxW, kBoxH, 8, kAmber);
    const int lines = wrapped(kFontTitle, kAmber, kBoxX + 12, kBoxY + 32, kBoxW - 24, 24, noticeTitle_, 2, true);
    wrapped(kFontText, kWhite, kBoxX + 12, kBoxY + 32 + lines * 24 + 6, kBoxW - 24, 18, noticeText_, 3, true);
}

void TftUi::drawError()
{
    const int16_t w = kW - 2 * kMargin;
    char header[24];
    snprintf(header, sizeof(header), "%s E%u", msg::kErrorPrefix, static_cast<unsigned>(screen_.error.code));

    canvas_->fillRect(0, kBarH, kW, 30, kRed);
    text(kFontTitle, kWhite, kMargin, kBarH + 23, header);

    int16_t y = kBarH + 30 + 22;
    const int titleLines = wrapped(kFontMedium, kWhite, kMargin, y, w, 19, screen_.title, 2);
    y += titleLines * 19 + 2;

    // details (e.g. HTTP code, path, UID)
    const int detailLines = titleLines > 1 ? 2 : 3;
    // details: the credit in Hungarian if an amount is attached (not enough credit), else the technical detail
    if (screen_.hasAmount)
    {
        char amountText[48];
        snprintf(amountText, sizeof(amountText), "%s %ld %s", msg::kBalance, (long)screen_.amount, msg::kCurrency);
        wrapped(kFontMedium, kWhite, kMargin, y + 4, w, 19, amountText, 1);
    }
    else if (screen_.error.detail[0])
    {
        wrapped(kFontSmall, kBarText, kMargin, y, w, 14, screen_.error.detail, detailLines);
    }

    // what to do, in the bottom line
    wrapped(kFontSmall, kAmber, kMargin, kH - 6, w, 14, screen_.text, 1);
}
