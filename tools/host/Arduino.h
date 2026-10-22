// Minimal Arduino stand-in so Adafruit GFX and U8g2_for_Adafruit_GFX compile on a PC
// for tools/test_compose.cpp. Not used by the firmware build.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
typedef bool boolean;
class __FlashStringHelper;
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
};
#include "Print.h"
