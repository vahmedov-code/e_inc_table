#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stddef.h>
#include "board.h"

// The frame is composed and sent in horizontal bands so only one band is in RAM:
// a whole 96 KB frame left too little heap for TLS to the CRM.
constexpr int FRAME_BANDS=4,BAND_ROWS=480/FRAME_BANDS;
constexpr size_t BAND_BYTES=FRAME_BYTES/FRAME_BANDS;
static_assert(FRAME_BYTES%FRAME_BANDS==0&&480%FRAME_BANDS==0,"whole rows per band");

// Native JD79665CA: 0 black, 1 white, 2 yellow, 3 red; MSB pixel first.
// Full-screen coordinates; pixels outside rows [top, top+rows) of the band are dropped.
class Frame : public Adafruit_GFX {
public:
  uint8_t* bytes;
  int top,rows;
  Frame(uint8_t* p,int firstRow,int rowCount) : Adafruit_GFX(800,480), bytes(p), top(firstRow), rows(rowCount) {}
  void drawPixel(int16_t x,int16_t y,uint16_t color) override {
    if(x<0 || x>=800 || y<top || y>=top+rows) return;
    size_t i=(y-top)*200+x/4; int shift=6-2*(x%4);
    bytes[i]=(bytes[i]&~(3<<shift))|((color&3)<<shift);
  }
};

class ScaledText : public Adafruit_GFX {
  Frame& target;
  int scale;
public:
  ScaledText(Frame& frame,int factor) : Adafruit_GFX(800/factor,480/factor),target(frame),scale(factor) {}
  void drawPixel(int16_t x,int16_t y,uint16_t color) override {
    target.fillRect(x*scale,y*scale,scale,scale,color);
  }
};
