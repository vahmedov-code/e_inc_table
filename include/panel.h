#pragma once
#include <Adafruit_GFX.h>
#include <SPI.h>
#include "board.h"
#include "render_trace.h"

// Native JD79665CA: 0 black, 1 white, 2 yellow, 3 red; MSB pixel first.
class Frame : public Adafruit_GFX {
public:
  uint8_t* bytes;
  explicit Frame(uint8_t* p) : Adafruit_GFX(800,480), bytes(p) {}
  void drawPixel(int16_t x,int16_t y,uint16_t color) override {
    if(x<0 || y<0 || x>=800 || y>=480) return;
    size_t i=y*200+x/4; int shift=6-2*(x%4);
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

class Panel {
  void byte(uint8_t b,bool data) {
    digitalWrite(EPD_DC,data); digitalWrite(EPD_CS,LOW);
    SPI.transfer(b); digitalWrite(EPD_CS,HIGH);
  }
  void cmd(uint8_t c) {byte(c,false);}
  void reg(uint8_t c,std::initializer_list<uint8_t> values) {
    cmd(c); for(auto b:values) byte(b,true);
  }
  bool ready() {
    delay(10); uint32_t start=millis();
    while(digitalRead(EPD_BUSY)==LOW) {
      if(millis()-start>45000) return false;
      delay(10);
    }
    return true;
  }
  void mark(uint8_t p){if(phase)phase(p);}
public:
  void (*phase)(uint8_t)=nullptr;
  // Protocol verified against Good Display A32-GDEM075F53-F-20260709.
  // Full refresh only. No F52 waveform or controller substitution.
  bool show(const uint8_t* data) {
    pinMode(EPD_BUSY,INPUT); pinMode(EPD_CS,OUTPUT);
    pinMode(EPD_DC,OUTPUT); pinMode(EPD_RST,OUTPUT);
    digitalWrite(EPD_CS,HIGH);digitalWrite(EPD_RST,HIGH);
    SPI.begin(EPD_SCK,-1,EPD_MOSI,EPD_CS);
    SPI.beginTransaction(SPISettings(4000000,MSBFIRST,SPI_MODE0));
    mark(PHASE_RESET);
    delay(100); digitalWrite(EPD_RST,LOW);delay(10);
    digitalWrite(EPD_RST,HIGH);delay(10);
    bool ok=ready();
    if(ok) {
      reg(0x4D,{0x78}); reg(0x00,{0x2F,0x29});reg(0xE3,{0x88});
      reg(0x50,{0x37});reg(0x61,{0x03,0x20,0x01,0xE0});
      reg(0x65,{0,0,0,0});reg(0xE9,{1});reg(0xF0,{0x5F});
      reg(0x30,{8});mark(PHASE_POWER_ON);cmd(0x04);ok=ready();
    }
    if(ok) {
      mark(PHASE_FRAME);cmd(0x10);
      for(size_t i=0;i<FRAME_BYTES;i++) {byte(data[i],true);if((i&1023)==0)delay(1);}
      mark(PHASE_REFRESH);reg(0x12,{0});ok=ready();
    }
    // Always attempt power off and deep sleep, even following a timeout.
    mark(PHASE_POWER_OFF);reg(0x02,{0}); bool poweredOff=ready();
    mark(PHASE_SLEEP);reg(0x07,{0xA5});
    digitalWrite(EPD_CS,HIGH);SPI.endTransaction();
    return ok && poweredOff;
  }
};
