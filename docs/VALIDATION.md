# Validation — 2026-10-21

## Update 0.2.1: schedule fix and battery estimate

Owner report: saving a valid one-event schedule (Thursday 21:00, closed) failed with "Проверьте дни, время, статусы…". Cause: `parseSchedule` takes a `const JsonDocument&`, and in ArduinoJson 7 `is<JsonArray>()` on a const variant is always false, so every schedule was rejected and the stored one was never loaded at boot. Confirmed on the host against the pinned ArduinoJson. The parser now lives in `include/schedule_json.h`, uses `is<JsonArrayConst>()`, and `tools/test_schedule.cpp` runs it with the real library (valid schedule accepted; empty enabled, duplicate, `image`, out-of-range day, minute and offset, non-array rejected). The test fails on the old check. CI runs it.

Battery: the owner's photo of the V3.0 board shows two resistors R8/R9 beside the battery connector with a trace to the module, most likely a divider. The schematic is still unavailable, so the firmware samples all ADC1 inputs and reports the one whose reading ×2 falls in 3.0–4.4 V as the battery, with an approximate rested Li-ion percentage. The panel shows it in the top bar and lists every input's millivolts under «Устройство». Unverified: which pin, the ratio, behaviour without a battery or while charging. A multimeter reading at the battery will calibrate it.

## Panel redesign 0.2.0

The owner asked for a panel where results are visible without scrolling back up, and for replaceable pictures on every status.

- Device state and connection sit in a sticky top bar (static on narrow screens); action results appear as a fixed toast, so no action requires scrolling to read its outcome. On desktop the preview card is sticky.
- Each status tile shows a thumbnail of what that status displays: the saved picture downloaded from the device, or an approximation of the built-in frame. Selecting a status shows it large; the same block replaces it with a PNG/JPEG or a text layout, saves it, or deletes it (`DELETE /asset`, new) to return to the built-in frame. Saving or publishing an unsaved picture uploads it first; switching status drops it.
- Storage preparation is offered inline where it is needed. The factory partition is erased, so formatting loses nothing on this board.
- The clock is shown in the schedule time zone (it was shown in UTC, which read three hours behind Moscow) and warns when it differs from the browser by more than 2 minutes.
- Quantisation maps near-gray pixels to black or white only; plain nearest colour turned antialiased text edges red.
- Radio-off during refresh is now standard behaviour (version 0.2.0).

Checked: node UI test (jsdom with real canvas) including per-status save, unsaved-picture drop, delete, missing storage, thumbnails from device, busy state, Moscow clock and gray quantisation; offline/syntax check; all three firmware environments compile with Arduino-ESP32 2.0.17. Desktop (1440×900) and phone (390 px) layouts were reviewed in headless Chromium against the simulator. Not yet run on the board.

## Owner log on 0.1.5-diag

With the radio stopped for the refresh, the first automatic refresh completed on USB power: `refresh` from t=22.5 s to `power_off` at t=42.8 s (about 20 s), `complete ok=1`, no reset, radio back with the office IP. One run only; repeat refreshes are pending.

Factory dump inspection (local analysis only, not stored in the repository): the factory app is an ESP-AT based Good Display demo ("E-paper Display WiFi Network Status", ESP-IDF 4.4.2, built 2022). The `spiffs` data partition at 0x290000 is entirely erased (0xFF), so formatting it loses no factory data; LittleFS reports `Corrupted dir pair` on erased flash. The factory panel driver was not disassembled.

Working explanation, not measured: AP+STA keeps the radio fully on (no modem sleep with an active AP), and that current together with the panel booster during the four-colour waveform exceeds what the board's 3.3 V path supplies; the panel alone does not. A rail measurement would confirm it.

## Owner log on 0.1.4-diag and update 0.1.5-diag

Facts from the owner's serial log (USB power, battery disconnected):

- The refresh reached `refresh` at t=20.9 s (reset, power-on and frame transfer completed; frame transfer took about 0.57 s). About 7 s into `refresh` the CH340 port vanished and the next boot reported `reset=brownout(9)`.
- Two further resets followed within about 15 s with no new render requested: another `brownout`, then `power_on` with the CH340 absent from Windows. A port watch earlier showed the same CH340 dropouts (about 0.8 s every 6-11 s) under 0.1.3.
- In ROM download mode the port stayed up long enough to flash.
- LittleFS mount fails with `Corrupted dir pair` (-84): the factory data partition is not a LittleFS image. Unrelated to the resets; storage stays disabled until the owner formats it explicitly.
- The owner reports the same restarts earlier with the battery connected.

Interpretation, not yet proven: the supply rail collapses during the panel refresh. The extra resets after the MCU reset fit a panel that keeps running its waveform after the MCU stops driving it. Whether Wi-Fi current contributes, and whether the init values draw abnormal booster current, is open.

Defect in 0.1.4-diag: the block was cleared at the first boot after an abort, so a later reset re-enabled automatic retry. 0.1.5-diag keeps it in NVS (`blocked`, `abortReset`) until a manual status request or a successful refresh.

Experiment in 0.1.5-diag: the radio is stopped (`WiFi.mode(WIFI_OFF)`) for the whole refresh and restarted afterwards, after a 1.5 s grace period for the HTTP reply. If brownouts stop, the combined panel and Wi-Fi load is the trigger; if they persist, the panel path alone collapses the supply. Wi-Fi setup and scan are refused while a refresh runs. Restart of mDNS after the radio returns is not verified.

## Display diagnostics 0.1.4-diag

Code review found a reset loop independent of its root cause: `hardware=true` persists in NVS, while `displayed` and `lastRefresh` start empty, so every boot requests a full refresh about one second after start, concurrently with AP/STA start-up. Any reset during refresh therefore repeats on the next boot.

- Each panel stage (`start`, `reset`, `power_on`, `frame`, `refresh`, `power_off`, `deep_sleep`) is written to NVS namespace `avior-trace` before it runs and cleared to `idle` afterwards. Serial prints time, heap and worker stack headroom per stage.
- A nonzero stage at boot means the previous refresh was interrupted by a reset. Normal firmware then blocks automatic retry and reports the stage and reset reason in `displayError`; a manual status request from the panel clears it. The `hardware` key is untouched. Recovery builds only read the trace.
- The first automatic refresh waits 20 s after boot so it no longer coincides with radio start-up. This separates two factors; it is not a fix.
- `/api/v1/state` adds `interruptedPhase`, `renderAborts`, `minHeap`, `workerStackFree`.
- `tools/serial_log.py` is a read-only log that keeps DTR/RTS released, survives USB re-enumeration and redacts boot secrets into `logs/` (ignored by Git).
- Brownout detector and watchdogs are unchanged. Framework config: brownout level 0, task watchdog 5 s on IDLE0 only; the display worker runs on core 1 and yields, so the panel code path alone cannot starve the task watchdog. Panic, heap or supply faults are not excluded.

Compiled for all three environments with Arduino-ESP32 2.0.17 and the pinned libraries. Not run on hardware. The F53 command sequence was not independently re-verified: the vendor archive is not public and the F52 driver was deliberately not used as a substitute.

## Recovery update 0.1.1

A separate `esp32-m075-recovery` build excludes the display worker and panel driver at preprocessing, bypasses the filesystem mount and STA connection, and exposes reset diagnostics in state and in a five-second serial heartbeat. Panel tests include disabled mutation controls and the visible reset reason in recovery. Both environments must compile before publication. Hardware stability and the cause of the user's restarts remain unverified until the owner installs recovery and sends the log.

## Wi-Fi isolation update 0.1.2

The owner reported a working recovery web panel and increasing uptime while powered by USB with the battery disconnected. This is a changed power condition as well as a changed firmware condition, so the earlier failure cannot yet be attributed to a specific component.

Added a distinct `esp32-m075-recovery-wifi` environment. It retains the display/FS exclusions, starts in AP-only mode and permits Wi-Fi setup/scan to enable STA on demand. Compile-time checks exercise the actual mutation policy for normal, AP-only recovery and Wi-Fi recovery builds. UI tests verify that Wi-Fi controls alone are enabled, display/format controls stay disabled, the password field is cleared after submission, async scan polling fills the selector, selected SSID fills the form, SSIDs are rendered as text, empty scan preserves manual entry and scan errors permit retry. Build all three environments before publication; then test actual office Wi-Fi and radio scanning on the owner's board under unchanged USB-only power.

## Connectivity reporting update 0.1.3

The owner reported reaching the AviorSign panel at an office LAN address while the serial heartbeat reported Arduino WiFi.status()=255. The agent's direct HTTP attempt to that address timed out; live device status was therefore not independently retrieved. The earlier conclusion that office Wi-Fi was absent based solely on 255 was not justified.

The firmware now samples SDK operating mode, actual association and ESP-NETIF address/interface readiness rather than using the cached Arduino status as the sole authority. Seven compile-time regression cases cover live connectivity with cached 255, disconnection with a stale IP/cached 3, AP-only mode, missing DHCP address, down interface and SDK read failures. UI tests cover the contradictory raw-status case and distinct pending-IP/unknown labels. All three build environments are checked. Physical verification of this update, and why the cached Arduino status disagreed, remain pending.

## Completed locally

- PlatformIO release build for ESP32-D0WD / 4 MB Flash, espressif32 6.10.0, Arduino-ESP32 2.0.17. No serial port was opened, firmware uploaded, Flash erased, filesystem formatted, or eFuse changed.
- The actual scheduler passes nine compile-time assertions: empty schedule, exact opening/closing boundaries, before-boundary behavior, overnight carry, Friday/Sunday and cyclic week wrap, unsorted events.
- Actual inline JavaScript passed Node.js syntax validation and the offline dependency check.
- Actual UI script executed in jsdom with a native Canvas implementation and a simulated API: invalid/valid token, session storage, status selection/publish, schedule save/disable and missing-time validation, all four native palette values, packed round trip, exact 96000-byte multipart frame, image upload, stale-image guard, logout. No JavaScript runtime errors.
- F53 command sequence, native dimensions, colors and BUSY polarity compared with the official `A32-GDEM075F53-F-20260709` ESP32 sample. GPIO mapping compared with the actual downloaded official schematic, including FPC pin numbers. The blank revision field is explicitly documented.
- Firmware filesystem partition label explicitly matches `littlefs` in the partition table. Startup mounts without automatic formatting. UI is gzip-embedded in firmware.
- Local flashing helper defaults to validation only and requires an external 4 MB backup and matching SHA256. Explicit COM port, `--write` and typed confirmation are required to invoke a local PlatformIO upload.
- Publish payload is an explicit allowlist of source, docs, test, workflow and configuration files. No backup, factory dump, private photo, credentials, compiled binary or dependency directory is included.

## Limits of these checks

- The ESP32 firmware has not run on the user's physical board. GPIO continuity on V3.0, panel identity, supply stability, boot strapping, actual four-color output, orientation, refresh timing, filesystem initialization and office Wi-Fi must be verified locally by the owner.
- The reported static RAM usage excludes the 96000-byte heap framebuffer, task stack, Wi-Fi and HTTP runtime allocations.
- Local browser access to the loopback preview server timed out in this environment, so desktop/mobile screenshot review was not completed. The responsive CSS and browser-facing logic were inspected; jsdom/Canvas checks are not a full browser layout or accessibility test.
- API simulation validates frontend behavior, not the ESP32 HTTP server, LittleFS atomic rename under power loss or long-duration hardware operation.
- No CRM integration has been connected. The local API is ready for a separately specified adapter.

## Repeat software checks

```powershell
python -m platformio run
python tools/check_project.py
npm install --prefix .pio/ui-tests --no-audit --no-fund jsdom@26.1.0 @napi-rs/canvas@0.1.80
node tools/test_ui.cjs
```

For UI-only manual review: `python tools/preview_server.py`, open `http://127.0.0.1:8765`, key `preview`. This simulator never contacts a board and supports JSON actions only. Review 375, 768 and 1440 pixel viewport widths, keyboard focus and feedback. It does not prove hardware compatibility.
