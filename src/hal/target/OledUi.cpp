#include "OledUi.h"

#include <Messages.h>

#include "../common/TextWrap.h"

namespace
{
    constexpr int16_t kW = 128;
    constexpr uint32_t kMinRenderIntervalMs = 100;
    constexpr uint32_t kAltIntervalMs = 2500;

    const uint8_t *const kFontSmall = u8g2_font_t0_11_te;
    const uint8_t *const kFontText = u8g2_font_6x13_te;
    const uint8_t *const kFontBig = u8g2_font_helvB14_te;
    const uint8_t *const kFontAmount = u8g2_font_helvB18_te;
    const uint8_t *const kFontIcon = u8g2_font_open_iconic_check_2x_t;
    // icon of the original project: 'C' = check mark
    constexpr uint16_t kIconCheck = 'C';
} // namespace

OledUi::OledUi(uint32_t i2cClockHz) : u8g2_(U8G2_R0, U8X8_PIN_NONE), i2cClockHz_(i2cClockHz) {}

bool OledUi::begin()
{
    u8g2_.setBusClock(i2cClockHz_);
    const bool ok = u8g2_.begin();
    u8g2_.enableUTF8Print();
    u8g2_.setFontMode(1);
    dirty_ = true;
    render();
    return ok;
}

void OledUi::show(const Screen &screen)
{
    screen_ = screen;
    showHint_ = false;
    altToggleMs_ = millis();
    dirty_ = true;
}

void OledUi::setStatus(const StatusInfo &status)
{
    if (status != status_)
    {
        status_ = status;
        // the error screen has no status bar, no need to redraw
        if (screen_.kind != ScreenKind::Error)
            dirty_ = true;
    }
}

void OledUi::loop(uint32_t nowMs)
{
    const bool hasAlt = (screen_.kind == ScreenKind::Error || screen_.kind == ScreenKind::Boot) && screen_.error.isError() &&
                        (screen_.error.detail[0] != '\0' || screen_.hasAmount);
    if (hasAlt && nowMs - altToggleMs_ >= kAltIntervalMs)
    {
        altToggleMs_ = nowMs;
        showHint_ = !showHint_;
        dirty_ = true;
    }
    if (dirty_ && nowMs - lastRenderMs_ >= kMinRenderIntervalMs)
    {
        lastRenderMs_ = nowMs;
        render();
    }
}

void OledUi::render()
{
    dirty_ = false;
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    if (screen_.kind == ScreenKind::Error)
    {
        drawError();
    }
    else
    {
        drawStatusBar();
        drawBody();
    }
    u8g2_.sendBuffer();
}

void OledUi::drawStatusBar()
{
    u8g2_.setFont(kFontSmall);
    // SSID and IP address alternate every 3 s (the clock redraws the bar every second)
    const bool showIp = status_.wifi && status_.ip[0] && ((millis() / 3000) % 2 == 1);
    const char *left = !status_.wifi ? msg::kNoWifi : (showIp ? status_.ip : status_.ssid);
    const int16_t clockW = status_.clock[0] ? u8g2_.getUTF8Width(status_.clock) : 0;
    textwrap::drawWrapped(u8g2_, 0, 9, kW - clockW - 4, 11, left, 1);
    if (clockW > 0)
        u8g2_.drawUTF8(kW - clockW, 9, status_.clock);
    u8g2_.drawHLine(0, 11, kW);
}

void OledUi::drawBody()
{
    char buf[48];
    const char *machine = status_.machineReady ? msg::kMachineReady : msg::kMachineBusy;

    switch (screen_.kind)
    {
    case ScreenKind::Idle:
        u8g2_.setFont(kFontBig);
        textwrap::drawCentered(u8g2_, 0, 34, kW, screen_.title);
        u8g2_.setFont(kFontSmall);
        snprintf(buf, sizeof(buf), "%s %ld %s", msg::kPriceLabel, (long)screen_.amount, msg::kCurrency);
        textwrap::drawCentered(u8g2_, 0, 50, kW, buf);
        textwrap::drawCentered(u8g2_, 0, 62, kW, machine);
        break;

    case ScreenKind::FreeIdle:
        u8g2_.setFont(kFontBig);
        textwrap::drawCentered(u8g2_, 0, 34, kW, screen_.title);
        u8g2_.setFont(kFontSmall);
        textwrap::drawWrapped(u8g2_, 0, 50, kW, 11, screen_.text, 1, true);
        textwrap::drawCentered(u8g2_, 0, 62, kW, machine);
        break;

    case ScreenKind::Checking:
        u8g2_.setFont(kFontIcon);
        u8g2_.drawGlyph(0, 34, kIconCheck);
        u8g2_.setFont(kFontText);
        textwrap::drawWrapped(u8g2_, 20, 29, kW - 20, 13, screen_.title, 1);
        u8g2_.setFont(kFontSmall);
        textwrap::drawWrapped(u8g2_, 0, 50, kW, 11, screen_.text, 2);
        break;

    case ScreenKind::Success:
        u8g2_.setFont(kFontText);
        textwrap::drawWrapped(u8g2_, 0, 24, kW, 13, screen_.title, 1, true);
        u8g2_.setFont(kFontAmount);
        snprintf(buf, sizeof(buf), "%ld %s", (long)screen_.amount, msg::kCurrency);
        textwrap::drawCentered(u8g2_, 0, 46, kW, buf);
        u8g2_.setFont(kFontSmall);
        textwrap::drawWrapped(u8g2_, 0, 62, kW, 11, screen_.text, 1, true);
        break;

    case ScreenKind::FreeVend:
        u8g2_.setFont(kFontBig);
        textwrap::drawCentered(u8g2_, 0, 34, kW, screen_.title);
        u8g2_.setFont(kFontSmall);
        textwrap::drawWrapped(u8g2_, 0, 51, kW, 11, screen_.text, 2, true);
        break;

    case ScreenKind::Boot:
    case ScreenKind::Error:
    default:
    {
        u8g2_.setFont(kFontText);
        const int titleLines = textwrap::drawWrapped(u8g2_, 0, 26, kW, 13, screen_.title, 2);
        u8g2_.setFont(kFontSmall);
        // the Hungarian hint alternates with the (English) technical detail
        const char *text = (showHint_ && screen_.error.detail[0] != '\0') ? screen_.error.detail : screen_.text;
        const int16_t y = titleLines > 1 ? 51 : 40;
        textwrap::drawWrapped(u8g2_, 0, y, kW, 11, text, titleLines > 1 ? 2 : 3);
        break;
    }
    }
}

void OledUi::drawError()
{
    char header[24];
    snprintf(header, sizeof(header), "%s E%u", msg::kErrorPrefix, static_cast<unsigned>(screen_.error.code));

    // header: inverted bar
    u8g2_.drawBox(0, 0, kW, 13);
    u8g2_.setDrawColor(0);
    u8g2_.setFont(kFontText);
    textwrap::drawCentered(u8g2_, 0, 11, kW, header);
    u8g2_.setDrawColor(1);

    u8g2_.setFont(kFontText);
    const int titleLines = textwrap::drawWrapped(u8g2_, 0, 26, kW, 12, screen_.title, 2);

    // what to do (screen_.text, Hungarian) alternates with the details every 2.5 s; the details are the
    // credit in Hungarian when an amount is attached (e.g. not enough credit), otherwise the technical detail
    char amountText[48];
    const char *detail = screen_.error.detail;
    if (screen_.hasAmount)
    {
        snprintf(amountText, sizeof(amountText), "%s %ld %s", msg::kBalance, (long)screen_.amount, msg::kCurrency);
        detail = amountText;
    }
    const bool hasDetail = detail[0] != '\0';
    const char *text = (hasDetail && !showHint_) ? detail : screen_.text;
    u8g2_.setFont(kFontSmall);
    const int16_t y = titleLines > 1 ? 51 : 40;
    textwrap::drawWrapped(u8g2_, 0, y, kW, 11, text, titleLines > 1 ? 2 : 3);
}
