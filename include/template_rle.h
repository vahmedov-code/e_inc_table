#pragma once
#include <stdint.h>
#include <stddef.h>

// Decodes bytes [from, from+count) of a run-length encoded template (tools/make_templates.py):
// control byte n < 128 is followed by n+1 literal bytes; n >= 128 by one byte repeated n-125
// times. Decoding restarts from the beginning for every band, which costs well under 1 ms.
inline bool unpackTemplate(const uint8_t* rle,size_t size,size_t from,size_t count,uint8_t* out){
  const size_t end=from+count;size_t pos=0,i=0;
  while(i<size&&pos<end){
    const uint8_t n=rle[i++];
    if(n<128){
      for(size_t k=0;k<=n&&i<size;k++,i++,pos++)if(pos>=from&&pos<end)out[pos-from]=rle[i];
    }else{
      if(i>=size)break;
      const uint8_t v=rle[i++];
      for(size_t k=0;k<(size_t)(n-125);k++,pos++)if(pos>=from&&pos<end)out[pos-from]=v;
    }
  }
  return pos>=end;
}
