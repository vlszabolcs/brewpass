#pragma once

#include <VendController.h>

#include "../hal/common/Hardware.h"
#include "../services/ConfigStore.h"
#include "../services/FirebaseCloud.h"
#include "../services/OtaService.h"
#include "../services/SystemClock.h"
#include "../services/WifiNetwork.h"

// Application assembly (composition root): services + hardware + controller.
class App
{
public:
    App();

    void begin();
    void loop();

private:
    void configureWatchdog();
    void printBanner();
    void reportHealth(uint32_t nowMs);
    bool handleOta();

    Hardware &hw_;
    WifiNetwork wifi_;
    SystemClock clock_;
    FirebaseCloud cloud_;
    ConfigStore configStore_;
    OtaService ota_;
    bool otaShown_ = false;
    uint8_t otaShownProgress_ = 0xFF;
    VendController controller_;
    uint32_t lastHealthMs_ = 0;
};
