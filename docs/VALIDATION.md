# Validation — 2026-10-21

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
