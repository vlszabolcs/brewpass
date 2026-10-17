#pragma once

#include "ErrorInfo.h"

// All user-facing display text in one place (UTF-8). The UI language is Hungarian on purpose
// (the end users are Hungarian); everything developer-facing is English.
namespace msg
{
    // Screen titles / fixed texts
    constexpr const char *kBootTitle = "Indítás...";
    constexpr const char *kPayPrompt = "Fizess!";
    constexpr const char *kPayHint = "Érintsd a kártyát az olvasóhoz";
    constexpr const char *kFreeTitle = "INGYENES";
    constexpr const char *kFreeHint = "Ingyenes mód - válassz italt";
    constexpr const char *kChecking = "Ellenőrzés...";
    constexpr const char *kCardRead = "Kártya beolvasva";
    constexpr const char *kBalance = "Egyenleg:";
    constexpr const char *kCurrency = "Ft";
    constexpr const char *kVendInProgress = "Kredit átadva - válassz italt";
    constexpr const char *kVendDone = "Jó kávézást!";
    constexpr const char *kFreeVend = "Kredit átadva (ingyenes)";
    constexpr const char *kErrorPrefix = "HIBA";

    constexpr const char *kPriceLabel = "Ár:";

    // Power off (test hardware) and OTA update
    constexpr const char *kPowerOffTitle = "Kikapcsolás...";
    constexpr const char *kPowerOffHint = "Bekapcsolás: PWR gomb";
    constexpr const char *kPowerOffUsbTitle = "USB táp";
    constexpr const char *kPowerOffUsbHint = "USB-ről táplálva nincs szükség kikapcsolásra";
    constexpr const char *kPowerOffFailedTitle = "Kikapcsolás sikertelen";
    constexpr const char *kPowerOffFailedHint = "Az eszköz tovább működik";
    constexpr const char *kPowerOffNoCharger = "A töltő IC nem válaszol";
    constexpr const char *kOtaTitle = "Firmware frissítés";
    constexpr const char *kOtaProgressSuffix = "- ne kapcsold ki!";

    // Sound on/off (test hardware)
    constexpr const char *kSoundTitle = "Hang";
    constexpr const char *kSoundOn = "bekapcsolva";
    constexpr const char *kSoundOff = "kikapcsolva";

    // Status bar
    constexpr const char *kStateReady = "kész";
    constexpr const char *kStateBusy = "foglalt";
    constexpr const char *kMachineReady = "Gép: kész";
    constexpr const char *kMachineBusy = "Gép: foglalt";
    constexpr const char *kNoWifi = "Nincs WiFi";

    // Short title for an error code
    inline const char *errorTitle(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::None: return "Nincs hiba";
        case ErrorCode::WifiDisconnected: return "Nincs WiFi kapcsolat";
        case ErrorCode::TimeNotSynced: return "Nincs pontos idő (NTP)";
        case ErrorCode::CloudNotReady: return "Firebase nem elérhető";
        case ErrorCode::CloudAuth: return "Firebase hitelesítési hiba";
        case ErrorCode::CloudRequest: return "Adatbázis-kérés sikertelen";
        case ErrorCode::CloudTimeout: return "Adatbázis időtúllépés";
        case ErrorCode::CloudData: return "Hibás adat az adatbázisban";
        case ErrorCode::CreditUpdateFailed: return "Egyenleg frissítése sikertelen";
        case ErrorCode::CardUnknown: return "Nem regisztrált kártya";
        case ErrorCode::CreditLow: return "Nincs elég kredit";
        case ErrorCode::ReaderFault: return "RFID olvasó hiba";
        case ErrorCode::MachineNotReady: return "A gép nem fogad kreditet";
        case ErrorCode::VendNotAcknowledged: return "A gép nem jelezte a kredit fogadását";
        case ErrorCode::MachineStuck: return "A gép nem tért vissza kész állapotba";
        case ErrorCode::ChargedNotVended: return "Terhelve, de a kredit nem lett átadva";
        case ErrorCode::ConfigInvalid: return "Érvénytelen konfiguráció";
        }
        return "Ismeretlen hiba";
    }

    // What to do / explanation for an error code
    inline const char *errorHint(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::None: return "";
        case ErrorCode::WifiDisconnected: return "Újracsatlakozás folyamatban...";
        case ErrorCode::TimeNotSynced: return "Időszinkronizálás folyamatban...";
        case ErrorCode::CloudNotReady: return "Bejelentkezés folyamatban...";
        case ErrorCode::CloudAuth: return "Ellenőrizd az API kulcsot és a felhasználót";
        case ErrorCode::CloudRequest: return "Próbáld újra";
        case ErrorCode::CloudTimeout: return "Gyenge kapcsolat? Próbáld újra";
        case ErrorCode::CloudData: return "Ellenőrizd a felhasználó adatait";
        case ErrorCode::CreditUpdateFailed: return "Nem történt terhelés, próbáld újra";
        case ErrorCode::CardUnknown: return "Kérd a kártya regisztrálását";
        case ErrorCode::CreditLow: return "Töltsd fel az egyenleged";
        case ErrorCode::ReaderFault: return "Olvasó újraindítása...";
        case ErrorCode::MachineNotReady: return "Várd meg, amíg a gép kész";
        case ErrorCode::VendNotAcknowledged: return "Ellenőrizd a gépet";
        case ErrorCode::MachineStuck: return "Ellenőrizd a gépet (hiba?)";
        case ErrorCode::ChargedNotVended: return "Szólj az adminisztrátornak";
        case ErrorCode::ConfigInvalid: return "Ellenőrizd a config/price értéket";
        }
        return "";
    }
} // namespace msg
