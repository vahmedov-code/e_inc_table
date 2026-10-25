# AviorSign API v1

Base URL: `http://DEVICE_IP/api/v1`. All endpoints, including reads, require `Authorization: Bearer DEVICE_TOKEN`. Obtain the token physically through Serial Monitor, 115200 baud. No default shared token, no token in query strings, no CORS. Serve the browser panel from the device itself.

JSON errors have shape `{"error":"message"}`. Typical codes: 400 invalid input, 401 bad/missing token, 404 missing asset/route, 409 precondition, 500 storage error. JSON bodies are limited to 8192 bytes by the application handler. The synchronous HTTP server is intended for trusted LAN clients, not hostile internet traffic.

Version 0.1.1 adds `recovery`, `resetReason` and `resetReasonCode` to state. The dedicated recovery build starts AP only, excludes the display task and filesystem mount, and returns HTTP 423 for every mutating endpoint. It does not erase or reset saved settings. See [RECOVERY.md](RECOVERY.md).

Version 0.1.2 adds `mode` and `wifiSetupAllowed`. The new `recovery-wifi` profile permits only authenticated Wi-Fi setup and scan operations for the next isolation test; every other mutation remains HTTP 423. It starts AP only: a scan enables STA for discovery without connecting, and `PUT /wifi` enables STA and initiates connection. Saved credentials do not trigger a connection on boot in this profile.

Version 0.1.3 changes connectivity reporting to a sampled SDK snapshot: operating mode from `esp_wifi_get_mode`, association from `esp_wifi_sta_get_ap_info`, and interface/IP from ESP-NETIF. `wifiConnected` is true only with a known enabled STA, confirmed association, an up network interface and a nonzero IPv4 address. This indicates LAN readiness, not Internet reachability. An old IP or Arduino cached status alone is insufficient.

`wifiStatus` values: `connected`, `associated` (link exists but interface/address is not ready), `disconnected`, `disabled`, `unknown` (required SDK read failed). The original event-cached status remains available as `wifiArduinoStatus`, together with `wifiAssociated`, `wifiNetifUp`, `wifiHasAddress` and SDK result fields `wifiModeError`, `wifiAssociationError`, `wifiIpError`. HTTP `ip` is 0.0.0.0 unless the station is ready. The heartbeat uses `wifi=connected` etc. and labels the original number separately as `arduino_wifi=255`. AP `clients` counts devices on AviorSign's own AP and does not determine office connectivity.

Version 0.2.0 stops the radio (AP and STA) for the duration of each panel refresh, about 25 s including the 1.5 s grace period after the request; HTTP is unreachable during that time and `busy` is true before and after. Wi-Fi setup and scan return 409 while a refresh runs. State adds `interruptedPhase`, `renderAborts`, `minHeap`, `workerStackFree`. After a reset during a refresh, automatic refresh stays blocked (reported in `displayError`) until a manual `POST /status` or a successful refresh.

Version 0.2.1 fixes `PUT /schedule`, which rejected every schedule (see VALIDATION.md), and adds battery fields to state: `adcProbe` maps each ADC1 input (GPIO 32, 33, 34, 35, 36, 39) to its calibrated millivolts, sampled every 5 s outside refreshes; `battery` `{pin, mv, percent, divider}` is present only when one input reads inside the Li-ion window behind the assumed 1:2 divider. Pin and ratio are inferred, not taken from a schematic.

Version 0.3.0:

- `POST /firmware` (multipart field `file`, an ESP32 application image such as `.pio/build/esp32-m075-v3/firmware.bin`) writes the image to the inactive OTA slot, verifies it and restarts about 1.5 s after the 200 reply. The first byte must be 0xE9; failures leave the running image untouched. Allowed in recovery builds too. The new image stays pending (`otaPending: true` in state) for 60 s of uptime; a reset during that minute makes the bootloader return to the previous image. Refused while a refresh runs; no refresh starts during an upload.
- `PUT /settings {"qrEnabled":true,"qrUrl":"https://…"}` (≤160 characters, http/https) stores the QR settings and redraws the current built-in frame. Saved pictures are never altered. State reports `qrEnabled` and `qrUrl`.
- `GET /qr` returns `{enabled,url,size,rows}`: the module matrix the firmware draws (rows of `0`/`1`), so clients can preview the exact code.
- `PUT /crm {"enabled":true,"url":"https://cms.avior.moscow","token":"…"}` connects the sign to Avior CRM (`token` optional: omitted keeps the stored one; changing URL or token resets the applied sequence numbers). State reports `crm {enabled,url,configured,error,lastOkSeconds}`; the token is never returned. While enabled, the sign posts its state to `<url>/api/sign/sync.php` about every 10 s (HTTPS verified against ISRG Root X1/X2, only with valid time, outside refreshes and uploads, with ≥45 KB free heap) and applies every CRM item whose `seq` is newer than the last applied one: status (like a manual `POST /status`), schedule, QR settings, pictures (downloaded from `<url>/api/sign/asset.php?slot=`). Local changes stay until the CRM changes the same item again.

Later in 0.3.0 (battery mode and access point):

- `PUT /power {"batterySave":true}` switches battery mode (also settable from the CRM). The sign then light-sleeps with the radio off and wakes at the next schedule event, every 15 minutes for the CRM (radio on only if the CRM is enabled or an hourly time sync is due), or on the board button (3-minute session). A timer wake sleeps again as soon as the CRM exchange and any refresh are done. Only mutating requests extend the awake time; an open panel polling `GET /state` does not. State reports `batterySave`, `wakeMinutes`, `wakeReason` (`boot`, `timer`, `button`, `crm`) and `awakeSeconds`.
- `POST /awake` keeps the sign awake for 10 minutes. The CRM «Разбудить» does the same at the sign's next check (not instantly: the radio is off while asleep).
- `POST /sleeptest {"seconds":180}` (30–600) sleeps exactly as battery mode does, to measure the board's sleep current with a meter in the battery lead. The board button ends it early.
- The access point is for setup only: it runs with no saved network, after the office network has been unreachable for a minute (not in battery mode), or for 10 minutes after holding the board button for one second; it is not dropped while a phone is connected to it. Recovery builds keep it always on. State reports `accessPoint` and `apName`.
- The QR code is drawn on every picture; saved pictures get a white plate under it. A discreet battery mark appears at the bottom right at 15 % or less.

Version 0.4.0:

- Built-in pictures are the owner's designs (`assets/templates/*.png`, packed by `tools/make_templates.py` into `include/templates.h`). The QR code moved to the square x 681, y 290, 99 px (bottom right, above the contact line). `GET /builtin?slot=open|closed|soon` streams the 96000-byte built-in frame without the QR code, for previews.
- Event log. `GET /events` returns `{next, boot, now, uptime, capacity, events[]}`, newest first; each event is `{seq, time, timeValid, boot, kind, code, value, extra, text}`. `time` is unix seconds when `timeValid`, otherwise seconds since that boot (the clock was not set yet; events of the running boot get the real time once it is). `kind`: `boot` (code = ESP-IDF reset reason), `before_reset` (what the previous run was doing at its last 5-second snapshot in RTC memory: uptime, battery, minimum heap, refresh or sleep in progress; only after an unexpected reset, not after power-on), `render_abort`, `wifi_down` (code = first Wi-Fi disconnect reason, extra = latest), `wifi_up`, `access_point`, `crm_error` (code = HTTP status or HTTPClient error, value = mbedTLS error), `crm_ok`, `refresh`, `refresh_fail`, `ota`, `battery`, `power`, `dropped`. `text` is the Russian description. The last 64 events live in NVS namespace `avior-log` and survive resets and power loss; at most 20 are stored per 10 minutes, the rest are counted in a `dropped` event; nothing is stored while NVS has fewer than 48 free entries, so the log never takes the room settings need. Recovery builds read the log but do not write it. State adds `log {next, boot, nvsUsed, nvsFree}`.
- 0.4.1: `battery.charge` in state and in the CRM report: `charging`, `full`, `discharging` or `unknown`. The board has no charger status line on an ESP32 input, so it is inferred from the voltage (`ChargeTracker` in `include/battery.h`): a step of 30 mV or more between two 15-second medians means USB plugged or unplugged (the owner's board reads about 50 mV higher on USB), a rise of 15 mV in 10 minutes means charging, a reading of 4.23 V or more means external power, and no rise for 10 minutes at 4.18 V or more while charging means full. Samples within 30 s of a refresh and 20 s of a wake or radio start are skipped. Changes are logged as `charge` events (code 1 discharging, 2 charging, 3 full with the charging time in extra).
- The CRM report adds `log {next, boot, events[]}` with up to 8 events newer than the last one the CRM confirmed. The CRM answers `eventsAck` (the last stored seq); the sign keeps it in NVS and sends the rest in the next exchanges. A CRM without the events migration ignores `log` and the sign keeps offering the same events.

## Status and completion

`GET /state` returns desired and displayed status, busy, displayError, hardwareConfirmed, storageReady, automatic, source, timeValid, unixTime, IP, heap, uptimeSeconds, cooldownSeconds and asset availability.

```json
{"desired":"open","displayed":"closed","busy":true,"displayError":"","automatic":false,"source":"manual"}
```

`POST /status`:

```json
{"status":"open"}
```

Accepted values: `open`, `closed`, `soon`, `image`. `image` requires a stored image. Every valid manual request disables automatic scheduling and persists the manual state. A successful HTTP response means the desired state was accepted; it **does not** mean the physical screen has finished refreshing. Poll `GET /state` every 5 seconds; completion is `displayed == desired && !busy && !displayError`. BUSY completion is electrical protocol confirmation, not optical verification. On first boot `displayed` is `unknown`, since the retained physical image cannot be read back.

Refreshes are separated by at least 60 seconds after completion. New desired states coalesce while the panel is busy/cooling down. Duplicate status requests do not refresh a correctly displayed frame. After timeout, a new manual status request or a scheduled status change is needed to retry.

## Schedule

`GET /schedule`, `PUT /schedule` (replace entire schedule atomically in RAM after validation; then persist NVS):

```json
{
  "enabled": true,
  "utcOffsetMinutes": 180,
  "events": [
    {"day":1,"minute":540,"status":"open"},
    {"day":1,"minute":1080,"status":"closed"},
    {"day":2,"minute":540,"status":"open"},
    {"day":2,"minute":1080,"status":"closed"}
  ]
}
```

`day`: Sunday=0 … Saturday=6; `minute`: 0…1439; status: `open`, `closed`, `soon`. Up to 32 entries, duplicate day/minute pairs rejected. Enabled schedule must contain at least one event. Latest event in a cyclic week wins; there is no implicit midnight reset. Add closure events explicitly. UTC offset range −720…840 minutes, fixed offset, no DST rules. A single event sets a status for the whole week.

`POST /clock {"unixTime":1792587600}` sets UTC wall time from an integer Unix timestamp. Accepted interval 1700000000…4102444800. NTP supplies time automatically when STA has internet. Schedule does nothing until the clock is valid. After full power loss without NTP, set the clock again. On reboot a persisted manual state is used until an enabled schedule has valid time.

## Images

`POST /assets?slot=open` with multipart form field `file`. Slots: `open`, `closed`, `soon`, `image`. Raw payload must be exactly **96000 bytes**, 800×480, row major, four pixels per byte, most significant pixel first. Native palette: 0 black, 1 white, 2 yellow, 3 red. Do not upload PNG/JPEG directly: use browser conversion or implement the same format in an adapter.

The file streams to `/upload.tmp` and is renamed into place after complete length validation. Failed/truncated/oversized uploads do not replace the existing slot. Upload start is rejected while a refresh is active. Storage must be initialized first. A current-status asset replacement invalidates the displayed cache and queues refresh; a different slot is stored for later. Slots do not change the status automatically. The client should send one file per request.

`DELETE /asset?slot=soon` removes a saved picture so the status returns to its built-in frame. Rejected with 409 while a refresh runs, and for `image` while `image` is the desired status. Deleting the current status's picture queues a refresh.

`GET /asset?slot=closed` returns the packed frame with `application/octet-stream`. Missing asset returns 404. Firmware-generated built-in status templates are not stored assets and cannot be downloaded through this endpoint.

## Setup

`POST /wifi/scan` starts an asynchronous visible-network scan; returns HTTP 202 and `{"scanning":true}`. Poll `GET /wifi/scan` approximately once a second. While running it returns HTTP 202; on completion HTTP 200:

```json
{"scanning":false,"networks":[{"ssid":"Office-Test","rssi":-42,"secure":true}]}
```

The response contains up to 20 nonempty distinct SSIDs with RSSI and a password/security indicator. Hidden SSIDs are omitted; manual SSID entry is retained. Results are consumed on retrieval and released from RAM; uncollected completed results expire after 30 seconds. GET without an outstanding scan returns 409. Scan failure returns 503. A connection request while a scan is running returns 409. Both scan endpoints require the same Bearer token and Wi-Fi capability as credential setup, and return 423 in AP-only recovery. Scanning enables the station radio while preserving the AP; no NVS credential write or connection is initiated by scanning alone.

`PUT /wifi {"ssid":"YOUR_NETWORK","password":"YOUR_PASSWORD"}` saves station credentials and initiates connection. SSID 1–32 bytes; password empty for an open network or 8–63 bytes. HTTP 202 only means accepted. Poll state for connection. AP remains enabled. Credentials are not returned by API.

`POST /hardware` enables this exact profile after local verification. Body:

```json
{"confirmation":"ESP32-M075-V3.0:F53:18,23,27,14,12,13"}
```

`POST /storage/init` is intentionally separate and destructive **only to the configured filesystem partition**. Requires `{"confirmation":"FORMAT_AVIOR_STORAGE"}`. It refuses while rendering or if storage already mounts. Keep the factory backup outside the repository. This endpoint is for owner setup, not CRM automation.

## Future Avior CRM adapter

No CRM credentials, proprietary API assumptions, inbound internet listeners or undocumented CRM routes are embedded. An external service should:

1. Store the device token in its secret store, never in frontend JavaScript or a repository.
2. Translate CRM business events to the three supported status values.
3. Call the device over a trusted LAN/VPN or an HTTPS gateway, never expose device HTTP publicly.
4. Poll state, respect cooldown, apply retry backoff, distinguish requested/physical completion, and report persistent errors for local inspection.
5. Decide who controls scheduling: manual CRM status requests disable device automation. To use device scheduling, push the complete validated week with `PUT /schedule`.

Example using PowerShell with the secret provided through an environment variable:

```powershell
$headers = @{ Authorization = "Bearer $env:AVIOR_DEVICE_TOKEN" }
Invoke-RestMethod -Uri 'http://192.168.4.1/api/v1/status' -Method Post `
  -Headers $headers -ContentType 'application/json' -Body '{"status":"open"}'
Invoke-RestMethod -Uri 'http://192.168.4.1/api/v1/state' -Headers $headers
```

Do not send the token in URLs or commit it. There are no signed webhooks, MQTT topics or CRM client in v0.1.0; those belong in a separately specified adapter.
