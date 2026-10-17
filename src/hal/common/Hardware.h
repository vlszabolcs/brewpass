#pragma once

#include <Interfaces.h>

// Peripherals of the selected hardware. The implementation lives in a different file per environment
// (hal/target/HardwareTarget.cpp or hal/tembed/HardwareTEmbed.cpp - chosen by build_src_filter).
struct Hardware
{
    ICardReader &reader;
    ICoinMechanism &coin;
    IUi &ui;
    IFeedback &feedback;
};

namespace board
{
    // Runs before everything else (power supply, shared bus CS pins, safe state of the outputs)
    void earlyInit();
    Hardware &hardware();
} // namespace board
