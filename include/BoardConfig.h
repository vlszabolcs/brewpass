#pragma once

// The platformio.ini environment decides which hardware pinout applies.
#if defined(BOARD_TARGET)
#include "boards/BoardTarget.h"
#elif defined(BOARD_TEMBED)
#include "boards/BoardTEmbed.h"
#else
#error "No hardware selected: -DBOARD_TARGET or -DBOARD_TEMBED is required (see platformio.ini)"
#endif
