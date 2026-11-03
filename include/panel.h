#pragma once
#include <SPI.h>
#include "board.h"
#include "frame.h"
#include "render_trace.h"

class Panel {
  void byte(uint8_t b,bool data) {
    digitalWrite(EPD_DC,data); digitalWrite(EPD_CS,LOW);
    SPI.transfer(b); digitalWrite(EPD_CS,HIGH);
  }
  void cmd(uint8_t c) {byte(c,false);}
  void reg(uint8_t c,std::initializer_list<uint8_t> values) {
    cmd(c); for(auto b:values) byte(b,true);
  }
  uint8_t current=0;
  // Waits for BUSY high. The refresh itself gets longer: the 4-colour panel slows down in the
  // cold, and owner logs 03-04.11 show it passing 45 s at night (normally about 28 s).
  bool ready(uint32_t limit=BUSY_LIMIT_MS) {
    delay(10); uint32_t start=millis();
    while(digitalRead(EPD_BUSY)==LOW) {
      if(millis()-start>limit) {if(!failPhase)failPhase=current;return false;}
      delay(10);
    }
    if(current==PHASE_REFRESH)refreshMs=millis()-start;
    return true;
  }
  void mark(uint8_t p){current=p;if(phase)phase(p);}
public:
  static constexpr uint32_t BUSY_LIMIT_MS=45000,REFRESH_LIMIT_MS=90000;
  void (*phase)(uint8_t)=nullptr;
  uint8_t failPhase=0;     // the stage whose BUSY wait timed out first, 0 if none
  uint32_t refreshMs=0;    // how long the panel took to refresh
  // Protocol verified against Good Display A32-GDEM075F53-F-20260709.
  // Full refresh only. No F52 waveform or controller substitution.
  // fill(buf,band) composes the next band into buf right before it is sent.
  template<typename Fill> bool show(uint8_t* buf,Fill fill) {
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
      for(int band=0;band<FRAME_BANDS;band++) {
        fill(buf,band);
        for(size_t i=0;i<BAND_BYTES;i++) {byte(buf[i],true);if((i&1023)==0)delay(1);}
      }
      mark(PHASE_REFRESH);reg(0x12,{0});ok=ready(REFRESH_LIMIT_MS);
    }
    // Always attempt power off and deep sleep, even following a timeout.
    mark(PHASE_POWER_OFF);reg(0x02,{0}); bool poweredOff=ready();
    mark(PHASE_SLEEP);reg(0x07,{0xA5});
    digitalWrite(EPD_CS,HIGH);SPI.endTransaction();
    return ok && poweredOff;
  }
};
