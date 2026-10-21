# Sources and dependencies

AviorSign source is MIT licensed. Dependencies are fetched by PlatformIO and are not vendored:

| Dependency | Pinned version | Upstream / license |
|---|---|---|
| Arduino-ESP32 | 2.0.17 (espressif32 6.10.0) | https://github.com/espressif/arduino-esp32 / LGPL-2.1 |
| ArduinoJson | 7.3.1 | https://github.com/bblanchon/ArduinoJson / MIT |
| Adafruit GFX | 1.12.1 | https://github.com/adafruit/Adafruit-GFX-Library / BSD |
| Adafruit BusIO | 1.17.4 | https://github.com/adafruit/Adafruit_BusIO / MIT |
| U8g2 | 2.36.5 | https://github.com/olikraus/u8g2 / BSD, individual font notices |
| U8g2_for_Adafruit_GFX | 1.8.0 | https://github.com/olikraus/U8g2_for_Adafruit_GFX / BSD |

The F53 driver is independently structured around hardware protocol facts verified against Good Display's A32-GDEM075F53-F-20260709 sample, https://www.good-display.com/companyfile/2093.html. The original archive and demo bitmap are not redistributed. The GPIO mapping is read from the manufacturer's schematic, https://www.good-display.com/companyfile/2075.html. No GxEPD2 source or F52 driver is used.

Before distributing firmware binaries, retain the upstream license notices, font notices and fulfill any applicable LGPL source/relinking obligations for the linked framework. This repository distributes source; dependencies are installed separately.
