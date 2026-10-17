#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

// Word wrapping of UTF-8 text by pixel width. Works with any U8g2-like renderer that has
// getUTF8Width(const char*) and drawUTF8(x, y, const char*) methods
// (both U8G2 and U8g2_for_TFT_eSPI).
namespace textwrap
{
    inline bool isUtf8Continuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

    template <class G>
    int16_t textWidth(G &g, const char *s, size_t len)
    {
        char buf[128];
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return g.getUTF8Width(buf);
    }

    template <class G>
    void drawLine(G &g, int16_t x, int16_t y, int16_t width, const char *s, size_t len, bool center)
    {
        char buf[128];
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        int16_t dx = 0;
        if (center)
        {
            const int16_t w = g.getUTF8Width(buf);
            if (w < width)
                dx = (width - w) / 2;
        }
        g.drawUTF8(x + dx, y, buf);
    }

    // Draws the text in at most maxLines lines; y is the baseline of the first line.
    // If it does not fit, the last line ends with "...". Returns the number of lines drawn.
    template <class G>
    int drawWrapped(G &g, int16_t x, int16_t y, int16_t width, int16_t lineHeight, const char *text, int maxLines,
                    bool center = false)
    {
        if (text == nullptr || *text == '\0' || maxLines <= 0)
            return 0;

        int lines = 0;
        const char *p = text;
        while (*p != '\0' && lines < maxLines)
        {
            while (*p == ' ')
                p++;
            if (*p == '\0')
                break;

            // find the longest line that ends on a word boundary
            size_t fit = 0;     // last word boundary that still fits
            size_t hardFit = 0; // last character boundary that still fits (if a single word is too long)
            size_t i = 0;
            while (p[i] != '\0')
            {
                size_t next = i + 1;
                while (p[next] != '\0' && isUtf8Continuation(p[next]))
                    next++;
                if (textWidth(g, p, next) > width)
                    break;
                hardFit = next;
                if (p[next] == ' ' || p[next] == '\0')
                    fit = next;
                i = next;
            }

            size_t len = fit > 0 ? fit : hardFit;
            if (len == 0)
                len = 1; // not even one character fits - make progress anyway
            const bool rest = p[len] != '\0';
            const bool lastLine = (lines == maxLines - 1);

            if (lastLine && rest)
            {
                // truncate with "..."
                char buf[128];
                size_t n = len < sizeof(buf) - 4 ? len : sizeof(buf) - 4;
                memcpy(buf, p, n);
                buf[n] = '\0';
                while (n > 0)
                {
                    char tmp[132];
                    snprintf(tmp, sizeof(tmp), "%s...", buf);
                    if (g.getUTF8Width(tmp) <= width)
                        break;
                    do
                    {
                        n--;
                    } while (n > 0 && isUtf8Continuation(buf[n]));
                    buf[n] = '\0';
                }
                char out[132];
                snprintf(out, sizeof(out), "%s...", buf);
                drawLine(g, x, y + lines * lineHeight, width, out, strlen(out), center);
                lines++;
                break;
            }

            drawLine(g, x, y + lines * lineHeight, width, p, len, center);
            lines++;
            p += len;
        }
        return lines;
    }

    template <class G>
    void drawCentered(G &g, int16_t x, int16_t y, int16_t width, const char *text)
    {
        drawLine(g, x, y, width, text, strlen(text), true);
    }
} // namespace textwrap
