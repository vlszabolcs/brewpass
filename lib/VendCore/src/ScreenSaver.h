#pragma once

#include <cstdint>
#include <cstring>

// OLED burn-in protection (hardware independent part, used by the target OledUi).
//
// - Pixel shift: while the normal screen is shown, the whole frame is moved by a few pixels at regular
//   intervals, so the static elements (status bar line, "Fizess!", price) do not always light the same pixels.
// - Screen saver: after idleTimeoutMs without activity the normal screen is replaced by a small text that
//   jumps to a new place every moveIntervalMs. Any activity (card tap, app request, new screen) wakes it.
struct ScreenSaverConfig
{
    uint32_t idleTimeoutMs = 60000;   // 0: the screen saver is disabled (the pixel shift still runs)
    uint32_t moveIntervalMs = 8000;   // the screen saver text jumps this often
    uint32_t shiftIntervalMs = 60000; // pixel shift step of the normal screen; 0: no pixel shift
    uint8_t maxShiftX = 2;            // pixel shift range: -maxShiftX..+maxShiftX
    uint8_t maxShiftY = 1;            // -maxShiftY..+maxShiftY
};

class ScreenSaver
{
public:
    explicit ScreenSaver(const ScreenSaverConfig &config = ScreenSaverConfig()) : config_(config) {}

    // Something happened on the device: show the normal screen again, restart the idle timer
    void activity(uint32_t nowMs)
    {
        lastActivityMs_ = nowMs;
        if (active_)
        {
            active_ = false;
            changed_ = true;
        }
    }

    // Call regularly. true if the picture has to be redrawn (mode change, new shift or saver position).
    bool update(uint32_t nowMs)
    {
        if (!active_ && config_.idleTimeoutMs > 0 && nowMs - lastActivityMs_ >= config_.idleTimeoutMs)
        {
            active_ = true;
            activeSinceMs_ = nowMs;
            ++session_; // every screen saver session starts at a different place
            changed_ = true;
        }
        const uint32_t step = active_ ? moveStep(nowMs) : shiftStep(nowMs);
        if (step != lastStep_)
        {
            lastStep_ = step;
            changed_ = true;
        }
        const bool c = changed_;
        changed_ = false;
        return c;
    }

    bool active() const { return active_; }

    // Offset of the normal screen at nowMs (pixel shift). The steps walk a snake path back and forth over the
    // (2*maxShiftX+1) x (2*maxShiftY+1) grid, so the picture only ever moves by one pixel at a time.
    void shift(uint32_t nowMs, int8_t &dx, int8_t &dy) const
    {
        const int w = 2 * config_.maxShiftX + 1;
        const int h = 2 * config_.maxShiftY + 1;
        const int n = w * h;
        int i = 0;
        if (n > 1)
        {
            const int period = 2 * (n - 1);
            i = static_cast<int>(shiftStep(nowMs) % static_cast<uint32_t>(period));
            if (i >= n)
                i = period - i; // way back
        }
        const int row = i / w;
        const int col = (row % 2 == 0) ? i % w : w - 1 - i % w;
        dx = static_cast<int8_t>(col - config_.maxShiftX);
        dy = static_cast<int8_t>(row - config_.maxShiftY);
    }

    // Top-left corner of the screen saver content (w x h pixels) on a screenW x screenH display at nowMs.
    // Pseudo-random, but the same for the whole move interval.
    void position(uint32_t nowMs, int16_t w, int16_t h, int16_t screenW, int16_t screenH, int16_t &x,
                  int16_t &y) const
    {
        uint32_t r = hash(session_ * 0x9E3779B9u + moveStep(nowMs));
        const int16_t freeW = screenW > w ? screenW - w : 0;
        const int16_t freeH = screenH > h ? screenH - h : 0;
        x = static_cast<int16_t>(r % static_cast<uint32_t>(freeW + 1));
        r = hash(r);
        y = static_cast<int16_t>(r % static_cast<uint32_t>(freeH + 1));
    }

private:
    uint32_t shiftStep(uint32_t nowMs) const { return config_.shiftIntervalMs ? nowMs / config_.shiftIntervalMs : 0; }
    uint32_t moveStep(uint32_t nowMs) const
    {
        return config_.moveIntervalMs ? (nowMs - activeSinceMs_) / config_.moveIntervalMs : 0;
    }
    static uint32_t hash(uint32_t v)
    {
        v ^= v >> 16;
        v *= 0x7FEB352Du;
        v ^= v >> 15;
        v *= 0x846CA68Bu;
        v ^= v >> 16;
        return v;
    }

    ScreenSaverConfig config_;
    bool active_ = false;
    bool changed_ = true;
    uint32_t lastActivityMs_ = 0;
    uint32_t activeSinceMs_ = 0;
    uint32_t session_ = 0;
    uint32_t lastStep_ = 0xFFFFFFFFu;
};

namespace pixelshift
{
    // Moves a monochrome page-format frame buffer (SSD1306 / SH1106 / U8g2 full buffer: `pages` rows of
    // `width` bytes, one byte = 8 vertical pixels, LSB on top) by dx (right) and dy (down) pixels in place.
    // Pixels moved off the screen are lost, the uncovered area is cleared.
    inline void shiftPages(uint8_t *buf, uint16_t width, uint8_t pages, int dx, int dy)
    {
        if (pages == 0 || pages > 8 || width == 0)
            return;
        const int height = pages * 8;
        if (dy != 0)
        {
            const uint64_t mask = height >= 64 ? ~0ull : ((1ull << height) - 1);
            for (uint16_t x = 0; x < width; ++x)
            {
                uint64_t col = 0;
                for (uint8_t p = 0; p < pages; ++p)
                    col |= static_cast<uint64_t>(buf[p * width + x]) << (8 * p);
                if (dy >= height || -dy >= height)
                    col = 0;
                else
                    col = dy > 0 ? (col << dy) : (col >> -dy);
                col &= mask;
                for (uint8_t p = 0; p < pages; ++p)
                    buf[p * width + x] = static_cast<uint8_t>(col >> (8 * p));
            }
        }
        if (dx != 0)
        {
            for (uint8_t p = 0; p < pages; ++p)
            {
                uint8_t *row = buf + p * width;
                if (dx >= width || -dx >= width)
                    memset(row, 0, width);
                else if (dx > 0)
                {
                    memmove(row + dx, row, width - dx);
                    memset(row, 0, dx);
                }
                else
                {
                    memmove(row, row - dx, width + dx);
                    memset(row + width + dx, 0, -dx);
                }
            }
        }
    }
} // namespace pixelshift
