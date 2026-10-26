// Host test of the firmware's schedule parser against the real ArduinoJson.
// Build: g++ -std=c++17 -I include -I <ArduinoJson>/src tools/test_schedule.cpp
#include <cassert>
#include <cstdio>
#include "schedule_json.h"

static bool parse(const char* json,int& count,int& offset,Event* out){
  JsonDocument d;if(deserializeJson(d,json))return false;
  return parseSchedule(d,out,count,offset);
}
int main(){
  Event e[32];int n=0,o=0;
  assert(parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[{"day":4,"minute":1260,"status":"closed"}]})",n,o,e));
  assert(n==1&&o==180&&e[0].day==4&&e[0].minute==1260&&e[0].status==1);
  assert(parse(R"({"enabled":false,"utcOffsetMinutes":0,"events":[]})",n,o,e)&&n==0);
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[{"day":1,"minute":540,"status":"open"},{"day":1,"minute":540,"status":"closed"}]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[{"day":1,"minute":540,"status":"image"}]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[{"day":7,"minute":540,"status":"open"}]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":[{"day":1,"minute":1440,"status":"open"}]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":900,"events":[{"day":1,"minute":540,"status":"open"}]})",n,o,e));
  assert(!parse(R"({"enabled":true,"utcOffsetMinutes":180,"events":{}})",n,o,e));
  // Manual status until the next event: Mon 10:00 open, Mon 21:00 closed.
  {Event w[2]={{1,600,0},{1,1260,1}};
   assert(minutesSinceEvent(w,2,1,1200)==600&&minutesSinceEvent(w,2,1,1261)==1&&minutesSinceEvent(w,2,1,599)==9419);
   uint32_t at=1800000000; // pressed at Mon 13:28, the open event in effect started 208 min before
   assert(manualHolds(at,at+3600,(208+60)*60));        // 14:28: still manual
   assert(!manualHolds(at,at+27720,2*60));            // 21:10: the 21:00 event started after the press
   assert(!manualHolds(0,at,100));}
  puts("Schedule parser and manual override (host, real ArduinoJson): PASS");
}
