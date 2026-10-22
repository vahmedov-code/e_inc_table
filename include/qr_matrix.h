#pragma once
#include <stdint.h>

// QR modules for at most version 10 (57×57): a URL of up to 160 characters at ECC M.
constexpr int QR_MAX_SIZE=57;
struct QrMatrix {
  int size=0;
  uint8_t bits[(QR_MAX_SIZE*QR_MAX_SIZE+7)/8]={0};
  bool get(int x,int y)const{int i=y*size+x;return (bits[i>>3]>>(i&7))&1;}
  void set(int x,int y){int i=y*size+x;bits[i>>3]|=1<<(i&7);}
};
// Largest whole-pixel module scale that fits the QR in `area` pixels.
constexpr int qrScale(int size,int area){return size>0?area/size:0;}
