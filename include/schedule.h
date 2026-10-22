#pragma once
#include <stdint.h>
struct Event { uint8_t day; uint16_t minute; uint8_t status; };
// Sunday=0; latest event in a cyclic week. Handles overnight and week wrap.
constexpr int scheduledStatus(const Event* events,int count,int weekday,int minute) {
  if(count==0) return -1;
  int now=weekday*1440+minute, bestAge=10081, result=-1;
  for(int i=0;i<count;i++) {
    int age=(now-events[i].day*1440-events[i].minute+10080)%10080;
    if(age<bestAge) {bestAge=age;result=events[i].status;}
  }
  return result;
}
// Minutes until the next event in the cyclic week (1..10080; an event at this very
// minute counts as a week ahead), or -1 without events. Used to time the battery-mode wake.
constexpr int minutesToNextEvent(const Event* events,int count,int weekday,int minute) {
  if(count==0) return -1;
  int now=weekday*1440+minute, best=10081;
  for(int i=0;i<count;i++) {
    int ahead=(events[i].day*1440+events[i].minute-now+10080)%10080;
    if(ahead==0) ahead=10080;
    if(ahead<best) best=ahead;
  }
  return best;
}
