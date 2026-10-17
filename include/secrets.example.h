#pragma once

// TEMPLATE - copy it as "secrets.h" into this folder and fill it in.
// secrets.h is listed in .gitignore, it never gets into version control.

// WiFi networks in PRIORITY ORDER: the most important first. The device tries them in order
// (skipping networks that are not visible) and stops at the first success. Empty SSID = skipped. At most 8 entries.
#define WIFI_NETWORKS                     \
    {                                     \
        {"ssid1", "password1"}, /* 1. */ \
        {"ssid2", "password2"}, /* 2. */ \
        {"ssid3", "password3"}, /* 3. */ \
    }

// Firebase project
#define FIREBASE_API_KEY "WEB_API_KEY"
#define FIREBASE_DATABASE_URL "https://PROJECT-default-rtdb.europe-west1.firebasedatabase.app"

// Firebase Authentication e-mail/password user (the user must exist in Authentication).
// If both are empty, the device connects without authentication (NoAuth) - only works with open database rules.
#define FIREBASE_USER_EMAIL "device@example.com"
#define FIREBASE_USER_PASSWORD "password"

// OTA update password (ArduinoOTA). Empty = OTA disabled.
// The same value is needed for uploading: RFID_OTA_PASSWORD environment variable (see platformio.ini)
#define OTA_PASSWORD "choose-a-long-password"

// Database paths (with a trailing slash; the key is appended)
#define DB_USER_PATH "/users/" // + <UID>   -> {credit, name, loan, log/<epoch>: remaining credit}
#define DB_LOG_PATH "/Log/"    // + <epoch> -> {e: event, d: details} - device status log
#define DB_STAT_PATH "/stat/"  // + free/<YYYYMMDD> -> daily number of free purchases
#define DB_CONF_PATH "/conf/"  // + price | free
#define DB_REMOTE_PATH "/remote/" // + <REMOTE_DEVICE_ID>/req|status|online -> purchase from the app (README)

// Id of this machine in /remote/<id>/ (purchase from the app). Several machines need different ids.
#define REMOTE_DEVICE_ID "brewpass"
