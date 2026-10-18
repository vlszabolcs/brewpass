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
/remote/<dev>/req|status|online    purchase from the app (see "Purchase from the app" below)
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
  | `remote` | the app request stream could not start (not enough heap) |

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

## Purchase from the app ("virtual card tap")

The app does **not** change the credit. It only asks the machine to act as if the card had been tapped; the
machine runs the request through the same state machine as a physical card (charge before the pulses, no pulses
after a failed write). There is no presence check: whoever knows a card UID can buy on it (decided on purpose).

`<dev>` is `REMOTE_DEVICE_ID` from `secrets.h` (default `machine1`, the id the admin app's user module uses); the paths follow
`coffeeapp/database.rules.json` of the project.

### 1. Is the machine available? `remote/<dev>/online`

```
remote/<dev>/online = {
  "ts":    1760700000000,   // server time (ms) of the last write
  "ready": true,            // a request would be taken right now
  "free":  false,           // free mode: no need to pay, the app should say so
  "price": 100              // Ft
}
```

Written every 60 s and within ~2 s when `ready` changes. `ready` is true only if the machine is idle, in normal
(paid) mode, accepts credit (inhibit LOW) and the request stream is connected. Treat the machine as offline if
`ts` is older than ~2.5 minutes (compare with the app's server time offset, `.info/serverTimeOffset`).

### 2. Request: `remote/<dev>/req/<pushId>`

```kotlin
val ref = db.getReference("remote/$dev/req").push()
ref.setValue(mapOf("cardUid" to "04A1B2C3", "ts" to ServerValue.TIMESTAMP))
val id = ref.key   // listen to remote/$dev/status/$id
```

- Exactly two fields: `cardUid` (upper-case hex, as in `users/<UID>`) and `ts` = **server timestamp**
  (the rules reject anything else, a client time included, and an unknown card).
- Only new requests can be written; the machine deletes the request as soon as it has handled it.

### 3. Progress: `remote/<dev>/status/<pushId>`

```
{ "state": "accepted", "ts": ... }                       taken over, user being checked ("Ellenőrzés...")
{ "state": "charged",  "ts": ..., "credit": 350 }        charged, credit sent: "Válassz italt a gépen!"
{ "state": "done",     "ts": ..., "credit": 350 }        the machine finished the drink
{ "state": "error",    "ts": ..., "code": 31 }           refused / failed, see the codes below
{ "state": "error",    "ts": ..., "code": 51, "credit": 350 }  failed AFTER the charge (credit present)
```

The states only move forward: `accepted` → `charged` → `done` | `error` (an `error` may come at any point).
`credit` is the credit after the charge and is only present once the account was charged.
If no status arrives within 30 s, the app should report "the machine did not answer".

| `code` | Meaning for the app | Charged? |
|---|---|---|
| 33 | refused: the machine is busy / not ready, free mode, a card was tapped at the same time, or the request is older than 20 s | no |
| 50 | the machine does not accept credit (inhibit HIGH) | no |
| 24 | malformed request | no |
| 30 / 31 | unregistered card / not enough credit | no |
| 20, 22, 23, 25 | database problem, nothing was charged (23 while charging: uncertain, see `/Log`) | no |
| 51 / 52 / 53 | charged, but the machine did not take the credit / got stuck / raised inhibit | **yes** |

Rules on the machine side: a request is taken only in the idle state (an error/result screen is cut short), it
is run at most once (the last handled `ts` + push key is saved in NVS, so a re-delivered request after a stream
restart or a reboot is only deleted), and a refused request does not disturb the display. Status entries are not
deleted by the machine (a few dozen bytes per purchase).

### 4. Credit transfer between two cards

The same request with two more fields moves credit from the sender's card to another card:

```kotlin
ref.setValue(mapOf("cardUid" to from, "ts" to ServerValue.TIMESTAMP, "to" to "11223344", "amount" to 500))
```

- `amount`: whole hundreds, 100 to 100 000 Ft. The sender's credit may not go negative (E31), unlike a purchase.
- The machine reads both credits and writes both cards in ONE PATCH on the database root:
  `users/<from>`: `credit`, `log/<k>` = new credit, `xfer/<k>` = `{"amount": -500, "peer": "<recipient's name>"}`,
  and the same for `users/<to>` with `+500`. `xfer/<k>` marks the log entry with the same key as a transfer
  (the apps show it as such instead of a purchase or a top-up).
- Taken in the idle state only, like a purchase, but free mode and inhibit do not matter (no drink). While it runs,
  cards and other requests are refused, so a purchase cannot overwrite the sender's credit in between.
- Status: `accepted` → `done` (`credit` = the sender's new credit) | `error`. Codes: 24 bad amount / same card,
  30 unknown card (either side), 31 not enough credit, 25 the write failed (nothing moved), 23 no answer (uncertain,
  see `/Log`), 33 refused.
- `online.transfer = true` tells the app that the firmware handles transfers. An older firmware would run the
  request as a purchase on the sender's card, so the app must not send a transfer without it.

**Memory:** the request stream is a third TLS connection (~40 KB). It is only started if at least
`kRemoteStreamMinFreeHeap` (70 KB) heap is free; otherwise `/Log` gets a `remote` event and `online.ready` stays
false. The free heap is printed on the serial console every minute (`[APP] State: ... | heap: ...`).
`kRemoteVendEnabled = false` in `include/Settings.h` turns the feature off.

## Test hardware: simulating the coffee machine

| Button | Function |
|---|---|
| encoder push button, **long press (1 s) on the "Fizess!" screen** | sound on/off (a short notice is shown; the setting is saved and survives a restart) |
| encoder push button | cup taken: after a successful credit entry the busy machine becomes ready again (inhibit → LOW). There is no automatic ready state (`kSimBrewMs = 0`); without a press for 180 s error **E52** follows |
| side button, short | machine busy/fault on/off (inhibit held HIGH, for testing **E50**) |
| side button, released after 1–5 s | "machine ignores the pulses" on/off (for testing **E51**) |
| side button, **held for 5 s** | **power off** (on battery only): the BQ25896 charger disconnects the battery (ship mode). **Power on: PWR button.** With USB power nothing is switched off, only a short notice is shown. The next `/Log` boot entry shows "power on" as the reason |

The simulated COIN pulses and the machine state appear on the serial console (`[SIM]` tag); the status bar of the display also shows the machine state.

**LED ring:** blue = card accepted, green = success, red = error. The speaker is on by default (`kSoundDefaultOn` in `include/boards/BoardTEmbed.h`) and can be toggled with a long encoder press on the pay screen; the LEDs always work.

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
| E33 | app purchase request refused (machine busy, free mode or request expired) - only sent to the app |
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
