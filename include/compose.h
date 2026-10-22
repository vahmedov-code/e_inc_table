#pragma once
#include <string.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "frame.h"
#include "qr_matrix.h"
#include "templates.h"
#include "template_rle.h"

struct ComposeInput {
  int status;          // 0 open, 1 closed, 2 soon, 3 image
  const QrMatrix* qr;  // nullptr: no QR code
  bool lowBattery;
};

// Composes rows [top, top+rows) of the frame: the saved picture read from `asset` (anything
// with seek/read, e.g. a LittleFS File) or the built-in template, then the live QR code in
// the template's QR square (over a saved picture with a white plate and the caption) and
// the low-battery mark. Bands composed one by one are byte-identical to the whole frame
// (tools/test_compose.cpp). Returns false when the picture could not be read.
template<typename Asset> bool composeRows(uint8_t* buf,int top,int rows,const ComposeInput& in,Asset* asset){
  const size_t bytes=(size_t)rows*200;bool ok=true;Frame f(buf,top,rows);const int s=in.status;
  if(asset){
    asset->seek((size_t)top*200);
    if(asset->read(buf,bytes)!=bytes){memset(buf,0x55,bytes);ok=false;}
  }else if(s>=0&&s<3){
    if(!unpackTemplate(TEMPLATES[s],TEMPLATE_SIZES[s],(size_t)top*200,bytes,buf)){memset(buf,0x55,bytes);ok=false;}
  }else{
    memset(buf,0x55,bytes);ok=false;
  }
  if(in.qr&&in.qr->size){
    const QrMatrix& qr=*in.qr;
    const int scale=qrScale(qr.size,QR_BOX_SIZE),px=qr.size*scale;
    const int x0=QR_BOX_X+(QR_BOX_SIZE-px)/2,y0=QR_BOX_Y+(QR_BOX_SIZE-px)/2;
    if(asset){
      f.fillRect(QR_BOX_X-4,QR_BOX_Y-4,QR_BOX_SIZE+8,QR_BOX_SIZE+30,1);
      U8G2_FOR_ADAFRUIT_GFX caption;caption.begin(f);caption.setFont(u8g2_font_unifont_t_cyrillic);
      caption.setForegroundColor(0);caption.setBackgroundColor(1);
      const char* line="Связаться";
      caption.setCursor(QR_BOX_X+QR_BOX_SIZE/2-caption.getUTF8Width(line)/2,QR_BOX_Y+QR_BOX_SIZE+16);caption.print(line);
    }
    for(int y=0;y<qr.size;y++)for(int x=0;x<qr.size;x++)if(qr.get(x,y))f.fillRect(x0+x*scale,y0+y*scale,scale,scale,0);
  }
  // Discreet low-battery mark for the owner, top right corner (free in every template).
  if(in.lowBattery){
    f.fillRect(744,4,52,24,1);f.drawRect(750,8,40,16,0);f.drawRect(751,9,38,14,0);f.fillRect(790,12,3,8,0);f.fillRect(753,11,7,10,3);
  }
  return ok;
}
