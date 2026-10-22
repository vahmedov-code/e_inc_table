// Host test: the frame composed band by band, as the firmware sends it to the panel, is
// byte-identical to the frame composed whole, for every status, with and without a saved
// picture, QR code and low-battery mark. Real Adafruit GFX and U8g2 fonts; Arduino is
// stubbed by tools/host. Optional argument: a directory to write sample frames to.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "compose.h"

struct MemoryAsset {
  const uint8_t* data; size_t pos=0;
  bool seek(size_t p){pos=p;return true;}
  size_t read(uint8_t* out,size_t n){size_t k=pos+n>FRAME_BYTES?FRAME_BYTES-pos:n;memcpy(out,data+pos,k);pos+=k;return k;}
};
static int pixel(const std::vector<uint8_t>& f,int x,int y){return (f[y*200+x/4]>>(6-2*(x%4)))&3;}

int main(int argc,char** argv){
  static const char* names[]={"Открыто","Закрыто","Скоро буду","Изображение"};
  QrMatrix qr;qr.size=29;for(int y=0;y<29;y++)for(int x=0;x<29;x++)if((x*7+y*13)%3==0)qr.set(x,y);
  std::vector<uint8_t> picture(FRAME_BYTES);for(size_t i=0;i<FRAME_BYTES;i++)picture[i]=(uint8_t)((i*2654435761u)>>24);
  int cases=0;
  for(int s=0;s<4;s++)for(int saved=0;saved<2;saved++)for(int withQr=0;withQr<2;withQr++)for(int low=0;low<2;low++){
    if(s==3&&!saved)continue;
    ComposeInput in{s,names[s],withQr?&qr:nullptr,low==1};
    std::vector<uint8_t> whole(FRAME_BYTES),banded(FRAME_BYTES),band(BAND_BYTES);
    MemoryAsset a1{picture.data()},a2{picture.data()};
    assert(composeRows(whole.data(),0,480,in,saved?&a1:(MemoryAsset*)nullptr));
    for(int b=0;b<FRAME_BANDS;b++){
      assert(composeRows(band.data(),b*BAND_ROWS,BAND_ROWS,in,saved?&a2:(MemoryAsset*)nullptr));
      memcpy(banded.data()+b*BAND_BYTES,band.data(),BAND_BYTES);
    }
    assert(whole==banded);cases++;
    if(s==1&&!saved&&withQr&&low){
      assert(pixel(whole,100,44)==3);                       // red bar of «Закрыто»
      assert(pixel(whole,557+3,130+3)==(qr.get(0,0)?0:1));   // QR module (0,0) at its place
      assert(pixel(whole,712,440)==3);                       // low-battery fill
    }
    if(argc>1&&withQr&&low){
      std::string path=std::string(argv[1])+"/frame-"+std::to_string(s)+(saved?"-saved":"")+".bin";
      FILE* out=fopen(path.c_str(),"wb");fwrite(whole.data(),1,FRAME_BYTES,out);fclose(out);
    }
  }
  printf("Compose: %d cases, banded frames identical to whole frames: PASS\n",cases);
}
