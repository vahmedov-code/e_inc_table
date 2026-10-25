// Host test of the charge detection from the battery voltage (include/battery.h).
#include <cassert>
#include <cstdio>
#include "battery.h"
struct Sim {ChargeTracker t;uint32_t now=0;
  ChargeState run(int seconds,int mvStart,int mvEnd,int noise=0){
    ChargeState s=t.state;
    for(int i=0;i*5<seconds;i++){now+=5000;int mv=mvStart+(mvEnd-mvStart)*i*5/(seconds>0?seconds:1);
      if(noise&&i%4==1)mv-=noise; // Wi-Fi burst during a sample
      s=t.update(now,mv);}
    return s;}
};
int main(){
  {Sim s;assert(s.run(60,3900,3900)==CHARGE_UNKNOWN);          // too early to tell
   assert(s.run(600,3900,3897)==CHARGE_OFF);                    // flat or falling for 10 minutes
   assert(s.run(30,3970,3970)==CHARGE_ON);                      // USB plugged: +70 mV step
   assert(s.run(3600,3975,4150,40)==CHARGE_ON);                 // charging, with Wi-Fi dips
   assert(s.run(900,4200,4205)==CHARGE_FULL);                   // plateau near 4.2 V
   assert(s.run(30,4150,4150)==CHARGE_OFF);}                    // unplugged: -55 mV step
  {Sim s;assert(s.run(30,4250,4250)==CHARGE_ON);}              // boot on USB, full battery reading 4.25 V
  {Sim s;s.run(120,3800,3800);assert(s.run(660,3820,3845)==CHARGE_ON);} // plugged while asleep: slow rise only
  {Sim s;s.run(700,3800,3790);assert(s.t.state==CHARGE_OFF);
   assert(s.run(1800,3790,3770,40)==CHARGE_OFF);}               // discharging with dips is never charging
  {int a[]={5,1,3},b[]={1,1,9},c[]={9,9,1};assert(ChargeTracker::median3(a)==3&&ChargeTracker::median3(b)==1&&ChargeTracker::median3(c)==9);}
  // Own discharge curve from the CRM: raw mV at 0, 10, ... 100 %.
  {int16_t c[CURVE_POINTS]={3480,3620,3680,3720,3760,3800,3850,3910,3980,4070,4190};
   assert(validCurve(c)&&curvePercent(c,3400)==0&&curvePercent(c,4250)==100&&curvePercent(c,3800)==50&&curvePercent(c,3830)==56);
   c[5]=3760;assert(!validCurve(c));c[5]=3800;c[0]=2500;assert(!validCurve(c));}
  assert(validScale(1000)&&validScale(988)&&!validScale(850)&&!validScale(1200));
  puts("Charge detection and battery calibration: PASS");
}
