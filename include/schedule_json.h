#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "schedule.h"

// Statuses a schedule may select: 0 open, 1 closed, 2 soon (same order as the panel slots).
inline int scheduleStatusIndex(const char* s){
  const char* keys[]={"open","closed","soon"};
  if(!s)return -1;
  for(int i=0;i<3;i++)if(!strcmp(s,keys[i]))return i;
  return -1;
}
// Validates a whole schedule before anything is replaced. Read-only views need the
// Const types: on a const document is<JsonArray>() is always false in ArduinoJson 7.
inline bool parseSchedule(const JsonDocument& d,Event* out,int& count,int& offset){
  if(!d["enabled"].is<bool>() || !d["utcOffsetMinutes"].is<int>() || !d["events"].is<JsonArrayConst>())return false;
  offset=d["utcOffsetMinutes"];if(offset < -720 || offset>840)return false;
  auto a=d["events"].as<JsonArrayConst>();if(a.size()>32)return false;count=0;
  for(auto e:a){
    if(!e["day"].is<int>() || !e["minute"].is<int>() || !e["status"].is<const char*>())return false;
    int day=e["day"],minute=e["minute"],s=scheduleStatusIndex(e["status"].as<const char*>());
    if(day<0||day>6||minute<0||minute>1439||s<0)return false;
    for(int i=0;i<count;i++)if(out[i].day==day&&out[i].minute==minute)return false;
    out[count++]={(uint8_t)day,(uint16_t)minute,(uint8_t)s};
  }
  return !d["enabled"].as<bool>() || count>0;
}
