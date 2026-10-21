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

OledUi::OledUi(uint32_t i2cClockHz, const ScreenSaverConfig &saver, uint8_t contrast, uint8_t saverContrast)
    : u8g2_(U8G2_R0, U8X8_PIN_NONE), i2cClockHz_(i2cClockHz), saver_(saver), contrast_(contrast),
      saverContrast_(saverContrast)
{
}

bool OledUi::begin()
{
    u8g2_.setBusClock(i2cClockHz_);
    const bool ok = u8g2_.begin();
    u8g2_.enableUTF8Print();
    u8g2_.setFontMode(1);
    saver_.activity(millis());
    dirty_ = true;
    render();
    return ok;
}

void OledUi::show(const Screen &screen)
{
    screen_ = screen;
    showHint_ = false;
    altToggleMs_ = millis();
    // every new screen (card tap, app request, result, error) wakes the screen saver
    saver_.activity(altToggleMs_);
    // the screen saver is off after boot; it is armed by the first purchase
    if (screen.kind == ScreenKind::Success || screen.kind == ScreenKind::FreeVend)
        saverArmed_ = true;
    dirty_ = true;
}

void OledUi::setStatus(const StatusInfo &status)
{
    if (status != status_)
    {
        // the machine changed state: someone is using it, wake the screen saver
        if (status.machineReady != status_.machineReady)
            saver_.activity(millis());
        status_ = status;
        // the error screen has no status bar, no need to redraw (the screen saver shows the clock)
        if (screen_.kind != ScreenKind::Error || saver_.active())
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
    // only on the pay screen (clock + "Fizess!"), never during a purchase: elsewhere the idle timer is held
    if (!saverArmed_ || screen_.kind != ScreenKind::Idle)
        saver_.activity(nowMs);
    if (saver_.update(nowMs))
        dirty_ = true;
    if (dirty_ && nowMs - lastRenderMs_ >= kMinRenderIntervalMs)
    {
        lastRenderMs_ = nowMs;
        render();
    }
}

void OledUi::render()
{
    dirty_ = false;
    const uint32_t now = millis();
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    if (saver_.active())
    {
        setContrast(saverContrast_);
        drawSaver(now);
    }
    else
    {
        setContrast(contrast_);
        if (screen_.kind == ScreenKind::Error)
        {
            drawError();
        }
        else
        {
            // after a card tap only the user matters: no status bar (network, clock)
            if (screen_.kind != ScreenKind::Checking && screen_.kind != ScreenKind::Success)
                drawStatusBar();
            drawBody();
        }
        // pixel shift: the static elements do not always light the same pixels
        int8_t dx, dy;
        saver_.shift(now, dx, dy);
        pixelshift::shiftPages(u8g2_.getBufferPtr(), u8g2_.getBufferTileWidth() * 8, u8g2_.getBufferTileHeight(), dx,
                               dy);
    }
    u8g2_.sendBuffer();
}

void OledUi::setContrast(uint8_t value)
{
    if (contrastSent_ == value)
        return;
    contrastSent_ = value;
    u8g2_.setContrast(value);
}

// Screen saver: a small text (what the screen is about + the clock) that jumps to a new place regularly
void OledUi::drawSaver(uint32_t nowMs)
{
    char line1[64];
    switch (screen_.kind)
    {
    case ScreenKind::Idle:
        snprintf(line1, sizeof(line1), "%s %ld %s", screen_.title, (long)screen_.amount, msg::kCurrency);
        break;
    case ScreenKind::Error:
        snprintf(line1, sizeof(line1), "%s E%u", msg::kErrorPrefix, static_cast<unsigned>(screen_.error.code));
        break;
    case ScreenKind::Boot:
        if (screen_.error.isError())
        {
            snprintf(line1, sizeof(line1), "%s E%u", msg::kErrorPrefix, static_cast<unsigned>(screen_.error.code));
            break;
        }
        // fall through
    default:
        snprintf(line1, sizeof(line1), "%s", screen_.title);
        break;
    }
    // "HH:MM" - the seconds would make the redraw pointless every second
    char line2[6] = {0};
    if (status_.clock[0])
        snprintf(line2, sizeof(line2), "%.5s", status_.clock);

    u8g2_.setFont(kFontText);
    const int16_t w1 = u8g2_.getUTF8Width(line1);
    u8g2_.setFont(kFontSmall);
    const int16_t w2 = line2[0] ? u8g2_.getUTF8Width(line2) : 0;
    const int16_t w = w1 > w2 ? w1 : w2;
    const int16_t h = line2[0] ? 24 : 13;
    int16_t x, y;
    saver_.position(nowMs, w, h, kW, 64, x, y);

    u8g2_.setFont(kFontText);
    u8g2_.drawUTF8(x + (w - w1) / 2, y + 10, line1);
    if (line2[0])
    {
        u8g2_.setFont(kFontSmall);
        u8g2_.drawUTF8(x + (w - w2) / 2, y + 22, line2);
    }
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
        // the user's name (large if it fits) and the remaining credit
        u8g2_.setFont(kFontBig);
        if (u8g2_.getUTF8Width(screen_.title) > kW)
            u8g2_.setFont(kFontText);
        textwrap::drawWrapped(u8g2_, 0, 18, kW, 13, screen_.title, 1, true);
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
