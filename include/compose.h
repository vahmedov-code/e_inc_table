#pragma once
#include <string.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "frame.h"
#include "qr_matrix.h"

struct ComposeInput {
  int status;          // 0 open, 1 closed, 2 soon, 3 image
  const char* name;    // status label for the built-in frame
  const QrMatrix* qr;  // nullptr: no QR code
  bool lowBattery;
};

// Composes rows [top, top+rows) of the frame: the saved picture read from `asset`
// (anything with seek/read, e.g. a LittleFS File) or the built-in frame, then the QR
// code (on a white plate over a saved picture) and the low-battery mark. Bands composed
// one by one are byte-identical to the whole frame (tools/test_compose.cpp).
// Returns false when the saved picture could not be read.
template<typename Asset> bool composeRows(uint8_t* buf,int top,int rows,const ComposeInput& in,Asset* asset){
  const size_t bytes=(size_t)rows*200;bool ok=true;Frame f(buf,top,rows);const int s=in.status;
  if(asset){
    asset->seek((size_t)top*200);
    if(asset->read(buf,bytes)!=bytes){memset(buf,0x55,bytes);ok=false;}
  }else{
    memset(buf,0x55,bytes);
    f.fillRect(40,40,720,8,s==1?3:s==2?2:0);
    f.setTextColor(0);f.setTextSize(5);f.setCursor(44,85);f.print("AVIOR");
    ScaledText text(f,4);U8G2_FOR_ADAFRUIT_GFX font;font.begin(text);font.setFont(u8g2_font_unifont_t_cyrillic);
    font.setForegroundColor(0);font.setBackgroundColor(1);font.setCursor(11,66);font.print(in.name);
    f.setTextSize(2);f.setCursor(44,410);f.print("avior.local");
  }
  if(in.qr&&in.qr->size){
    const QrMatrix& qr=*in.qr;const int scale=qrScale(qr.size,220),px=qr.size*scale,x0=760-px,y0=130;
    if(asset)f.fillRect(x0-14,y0-14,px+28,px+70,1);
    for(int y=0;y<qr.size;y++)for(int x=0;x<qr.size;x++)if(qr.get(x,y))f.fillRect(x0+x*scale,y0+y*scale,scale,scale,0);
    ScaledText small(f,2);U8G2_FOR_ADAFRUIT_GFX caption;caption.begin(small);caption.setFont(u8g2_font_unifont_t_cyrillic);
    caption.setForegroundColor(0);caption.setBackgroundColor(1);
    const char* line=s==0?"Мы на связи":"Напишите нам";
    caption.setCursor((x0+px/2)/2-caption.getUTF8Width(line)/2,(y0+px+36)/2);caption.print(line);
  }
  // Discreet low-battery mark for the owner, bottom right.
  if(in.lowBattery){
    f.fillRect(700,424,64,32,1);f.drawRect(706,430,44,20,0);f.drawRect(707,431,42,18,0);f.fillRect(750,435,4,10,0);f.fillRect(709,433,8,14,3);
  }
  return ok;
}
