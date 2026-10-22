#pragma once

#include <cstddef>

// Survives a watchdog reset: what the two watched tasks did last, so the boot event after a
// "task watchdog (hang)" restart tells which task got stuck and where.
namespace hangtrace
{
    // network task: call before a step that may block (DB request, WiFi, Firebase loop); phase must be short
    void cloud(const char *phase);
    // main loop: once per pass
    void loopTick();
    // Call once at boot, before the tasks start. After a task watchdog reset: a short description
    // (e.g. "net: Poll app requests 46s, loop 0s"), otherwise an empty string.
    void report(char *out, size_t size);
} // namespace hangtrace
