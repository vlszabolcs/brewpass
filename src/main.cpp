// BrewPass - RFID based coin validator emulator (replacing an NRI G-13) for a Saeco Royal Office coffee machine.
// The hardware is selected by the platformio.ini environment (target / tembed).
#include <Arduino.h>

#include "app/App.h"

namespace
{
    App *app = nullptr;
}

void setup()
{
    board::earlyInit(); // safe state of the outputs, peripheral power - before anything else
    Serial.begin(115200);
    delay(200); // give the USB CDC (T-Embed) time to attach

    static App instance;
    app = &instance;
    app->begin();
}

void loop()
{
    app->loop();
}
