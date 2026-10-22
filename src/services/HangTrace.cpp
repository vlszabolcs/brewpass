#include "HangTrace.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

namespace
{
    constexpr uint32_t kMagic = 0x4A6E7AC3;

    struct Trace
    {
        uint32_t magic;
        uint32_t cloudMs; // millis() of the last network task step
        uint32_t loopMs;  // millis() of the last main loop pass
        char phase[24];   // the last network task step
    };
    RTC_NOINIT_ATTR Trace gTrace;
} // namespace

namespace hangtrace
{
    void cloud(const char *phase)
    {
        gTrace.cloudMs = millis();
        strlcpy(gTrace.phase, phase, sizeof(gTrace.phase));
    }

    void loopTick() { gTrace.loopMs = millis(); }

    void report(char *out, size_t size)
    {
        out[0] = '\0';
        if (esp_reset_reason() == ESP_RST_TASK_WDT && gTrace.magic == kMagic)
        {
            gTrace.phase[sizeof(gTrace.phase) - 1] = '\0';
            // ages relative to the newest stamp: the stuck task is the one with the old stamp
            const uint32_t newest = gTrace.cloudMs > gTrace.loopMs ? gTrace.cloudMs : gTrace.loopMs;
            snprintf(out, size, "net: %s %lus, loop %lus", gTrace.phase[0] ? gTrace.phase : "-",
                     (unsigned long)((newest - gTrace.cloudMs) / 1000), (unsigned long)((newest - gTrace.loopMs) / 1000));
        }
        gTrace.magic = kMagic;
        gTrace.cloudMs = 0;
        gTrace.loopMs = 0;
        gTrace.phase[0] = '\0';
    }
} // namespace hangtrace
