# BrewPass

Object-oriented rewrite of [rfidChecker](../rfidChecker-main). It is an RFID based coin validator emulator that replaces the NRI G-13.mft coin validator of a Saeco Royal Office coffee machine. User data, configuration and logs are stored in the Firebase Realtime Database.

The developer-facing language (code, comments, serial log, `/Log` events, docs) is English. The text on the device display is Hungarian, for the end users; it lives in `lib/VendCore/src/Messages.h`.

## Building

| Environment | Hardware | Command |
|---|---|---|
| `target` | ESP32 (WROOM-32) + RC522 + SH1106 OLED + buzzer + coffee machine | `pio run -e target -t upload` |
| `tembed` | LilyGO T-Embed CC1101 Plus (ESP32-S3, PN532, ST7789) | `pio run -e tembed -t upload` |
| `native` | unit tests of the hardware independent logic (on the PC) | `pio test -e native` |

First step: copy `include/secrets.example.h` to `include/secrets.h` and fill in the WiFi networks, the Firebase data and the database paths. `secrets.h` is not under version control.

> Firebase needs an **e-mail/password user** that already exists in Firebase Authentication. The original code called `signUp` at every boot; this version signs in (`UserAuth`). If the e-mail is left empty, the device connects without authentication (`NoAuth`).

## OTA update (over WiFi)

After the first USB upload, the firmware can also be updated over WiFi (ArduinoOTA).

- The password is in `secrets.h` (`OTA_PASSWORD`). If it is empty, OTA is disabled.
- The device address is `brewpass.local` (mDNS). The IP address is also printed on the serial console at startup (`[OTA] OTA ready: ...`).

```bash
export RFID_OTA_PASSWORD='<OTA_PASSWORD value from secrets.h>'
pio run -e tembed_ota -t upload                                  # or: -e target_ota
pio run -e tembed_ota -t upload --upload-port 192.168.50.101     # if mDNS does not work
```

- **Updates only in an idle state.** During a purchase the device does not accept the request (espota stops with "No response from device"); retry later.
- **Purchases are paused during an update.** The display shows the update progress. On success the device restarts; if the update is interrupted it returns to normal operation.
- If espota reports `Host ... Not Found` right away on macOS, `ping` the device once and retry (the ARP entry was missing).

## Structure

```
lib/VendCore/            hardware independent core (also builds and is tested natively)
  Interfaces.h           ICardReader, ICoinMechanism, IUi, IFeedback, ICloud, INetwork, IClock
  VendController.*       non-blocking state machine of the purchase flow
  PurchasePolicy.h       purchase rule (the original one: credit > 0)
  ErrorInfo.h            error codes + English names
  Messages.h             display texts (Hungarian)
src/
  main.cpp, app/App.*    assembly, watchdog, diagnostics, boot event
  services/              WifiNetwork (several networks, in priority order), SystemClock (SNTP + TZ),
                         FirebaseCloud (FirebaseClient, own FreeRTOS task, config stream), ConfigStore (NVS),
                         OtaService (ArduinoOTA)
  hal/common/            GpioCoinMechanism (esp_timer pulses), PatternFeedback, TextWrap, Button
  hal/target/            Mfrc522Reader, OledUi (U8g2), BuzzerFeedback
  hal/tembed/            Pn532Reader, TftUi (TFT_eSPI + U8g2 fonts), TEmbedFeedback (I2S + WS2812),
                         SimCoinMechanism (coffee machine simulated with buttons)
include/
  Settings.h             timings, time zone, watchdog etc.
  boards/*.h             pinout per hardware
```

**Threads:**
- **Core 0, `cloud` task:** WiFi connection, time sync, Firebase sign-in, config stream, OTA and every database operation. These are slow and may block.
- **Core 1, `loop`:** card reading, machine interface, display and sound. Never blocks.
- The two communicate through FreeRTOS queues.

## Database structure (Firebase RTDB)

```
/users/<UID>                       card UID (upper-case hex)
    credit : 200                   current credit (Ft)
    name   : "TestElek"
    loan   : 0                     (not used yet)
    log/<epoch> : 350              log: the credit AFTER the event (purchase and admin top-up alike)
/Log/<epoch> : {"e": "boot", "d": "T-Embed v2.0.0, reason: power on"}
                                   device status log: boot, wifi, config, reader, error, ota
/conf                              price, free (+ coffeeBeans - not used by the device); watched by a stream
/stat/free/<YYYYMMDD> : 37         daily number of free-mode purchases (server-side +1)
```

- **Per card tap:** `GET /users/<UID>/credit` and `GET /users/<UID>/name`, so the log is never downloaded. (`shallow=true` cannot be used for this: it replaces numbers with `true` too.)
- **On purchase:** a single PATCH writes the credit and the log together:
  `{"credit": 350, "log/<epoch>": 350}`.
- **The event type** follows from the difference of neighbouring entries: decreased = purchase, increased = top-up. Therefore the admin app must log **every** credit change.
- **A failed purchase** (no credit) is not written to the database.
- **`/Log` is the device status log** (purchases no longer go there). Events:

  | Event (`e`) | When |
  |---|---|
  | `boot` | every startup: version, reset reason, boot after an OTA update |
  | `wifi` | first connection and every reconnect (with the offline duration) |
  | `config` | price or mode change |
  | `reader` | reader fault and recovery (at most every 5 minutes) |
  | `error` | E25, E51, E52, E53 and an uncertain charge |
  | `ota` | failed update |

  Events created before the time sync / network may wait up to 30 minutes in the queue; they are written with the time of writing.

- **Configuration:** a full fetch only at startup and after a WiFi reconnect; changes in between arrive within ~1 s through the `/conf` stream. A change during a purchase is applied after the purchase ends.

## Purchase flow

1. **Card tap.** Accept sound, then a check: if the machine does not accept credit (inhibit HIGH), error **E50** is shown.
2. **Fetch user** (`users/<UID>/credit`, `/name`).
   - No such user: **E30**.
   - Credit not positive: **E31**.
3. **Charge:** `credit -= price`, log `users/<UID>/log/<epoch>` = new credit (in the same PATCH).
4. **Credit hand-over:** if the machine still accepts credit, 2 pulses are sent (50 ms LOW / 50 ms HIGH).
5. The machine raises inhibit HIGH (credit accepted). When it goes back LOW, the drink is done.

**Free mode:** if inhibit is LOW, the machine gets credit right away, then the daily counter (`stat/free/<YYYYMMDD>`) is incremented.

## Test hardware: simulating the coffee machine

| Button | Function |
|---|---|
| encoder push button | cup taken: after a successful credit entry the busy machine becomes ready again (inhibit → LOW). There is no automatic ready state (`kSimBrewMs = 0`); without a press for 180 s error **E52** follows |
| side button, short | machine busy/fault on/off (inhibit held HIGH, for testing **E50**) |
| side button, released after 1–5 s | "machine ignores the pulses" on/off (for testing **E51**) |
| side button, **held for 5 s** | **power off** (on battery only): the BQ25896 charger disconnects the battery (ship mode). **Power on: PWR button.** With USB power nothing is switched off, only a short notice is shown. The next `/Log` boot entry shows "power on" as the reason |

The simulated COIN pulses and the machine state appear on the serial console (`[SIM]` tag); the status bar of the display also shows the machine state.

**LED ring:** blue = card accepted, green = success, red = error. The speaker is disabled (`kSoundEnabled = false` in `include/boards/BoardTEmbed.h`).

## Error codes

The display shows `HIBA E<code>` + a short Hungarian title + what to do. The technical detail (e.g. HTTP code, path, UID) is shown as a secondary line in English and is also printed on the serial console.

| Code | Meaning |
|---|---|
| E10 | no WiFi (the detail contains the last disconnect reason, e.g. wrong password or weak signal) |
| E11 | no exact time (NTP) |
| E20 / E21 | Firebase unavailable / authentication error |
| E22 / E23 / E24 | database request error / timeout / invalid data (e.g. missing `credit`) |
| E25 | updating the credit failed - **no charge and no credit hand-over** |
| E30 / E31 | unregistered card / not enough credit |
| E40 | RFID reader fault (automatic recovery) |
| E50 | the machine does not accept credit (inhibit HIGH) |
| E51 | after the pulses the machine did not signal that it accepted the credit (2 s) |
| E52 | the machine did not return to ready after 180 s |
| E53 | charged, but the machine raised inhibit meanwhile, so no credit was sent |
| E60 | invalid configuration (`conf/price` ≤ 0 or missing) |

## Changes compared to the original

**Kept:**
- purchase rule: `credit > 0` (the loan logic is left out on request);
- order: charge and log **before** the credit pulses;
- pulse timing, pinout, sounds.

**Changed data format** (see "Database structure"; the Coffee Admin app has to be updated):
- purchases are logged under `users/<UID>/log` instead of `/Log`, the `update` field is gone;
- `/Log` holds device status events;
- free purchases are counted in `/stat/free`.

**Fixed (stability):**
- Every wait has a timeout. The old `while (digitalRead(mIN))` could hang the device forever.
- If updating the credit fails, no credit is sent. Previously the coffee was dispensed anyway.
- On a cloud fetch error the old code continued with `price = -1`, so a purchase **added** 1 Ft. Now the last valid price is kept (also saved in NVS), and a price ≤ 0 is rejected.
- The background task does not execute a request that waited too long in the queue, so no charge can happen after the controller timed out.
- Log keys are strictly increasing epochs, so entries in the same second do not overwrite each other. A failed device-event write is retried 3 times; if all fail, its content goes to the console.
- Configuration changes arrive through a stream without a restart.
- The time zone is a POSIX TZ: DST changes are correct at runtime too.
- The RC522 and the PN532 have a health check and recover automatically after a hang.
- A task watchdog (45 s) is active. At startup the reset reason and the heap state are printed, and a status report every minute.
- The COIN pulses are timed by esp_timer, so main-loop load does not distort them. No short LOW glitch on the output at startup.
- A card is only taken into account once (2 s hold-off for the same UID).

## Hardware notes (target hardware)

- **COIN output (GPIO25):** the machine pulls the line up to 5 V, so `kCoinOutOpenDrain = true` is recommended (`include/boards/BoardTarget.h`). The default is the original push-pull.
- **Inhibit input (GPIO34):** input only, no internal pull-up. Make sure the signal level does not exceed 3.3 V.
- **RC522 SS (GPIO2):** this is a strapping pin. If there are boot problems, move it to another pin.
