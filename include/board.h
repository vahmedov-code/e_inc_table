#pragma once
// Good Display ESP32-M075-SCH-20250704, published 2026-06-15.
// Revision field in the published schematic is blank: verify continuity on V3.0.
constexpr int EPD_SCK = 18;
constexpr int EPD_MOSI = 23;
constexpr int EPD_CS = 27;
constexpr int EPD_DC = 14;
constexpr int EPD_RST = 12;
constexpr int EPD_BUSY = 13;
constexpr size_t FRAME_BYTES = 800 * 480 / 4;
