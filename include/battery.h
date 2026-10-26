#pragma once
#include <stdint.h>

// R8/R9 next to the battery connector look like a divider to an ESP32 ADC input
// (owner photo, V3.0). Pin and ratio are not confirmed by a schematic: the firmware
// samples every ADC1 input and treats the one in the Li-ion window as the battery tap.
constexpr int BATTERY_DIVIDER=2; // assumes equal resistors; confirm with a multimeter
constexpr uint8_t ADC_PROBE_PINS[]={32,33,34,35,36,39}; // ADC1 only: ADC2 is unusable with Wi-Fi on
constexpr int ADC_PROBE_COUNT=sizeof(ADC_PROBE_PINS)/sizeof(ADC_PROBE_PINS[0]);

constexpr bool plausibleBattery(int pinMv){return pinMv*BATTERY_DIVIDER>=3000&&pinMv*BATTERY_DIVIDER<=4400;}

// Rested Li-ion voltage to charge, linear between points. Readings while charging
// or under load are only an estimate.
constexpr int batteryPercent(int mv){
  constexpr int v[]={3300,3500,3600,3650,3700,3750,3800,3900,4000,4100,4200};
  constexpr int p[]={0,5,12,20,30,40,50,65,78,90,100};
  if(mv<=v[0])return 0;
  for(int i=1;i<11;i++)if(mv<=v[i])return p[i-1]+(p[i]-p[i-1])*(mv-v[i-1])/(v[i]-v[i-1]);
  return 100;
}

// Charging, inferred from the battery voltage alone: the board has no charger status line
// on an ESP32 input (owner check on 0.4.0: GPIO32/34 do not change with USB). With USB
// the reading sits about 50 mV higher (owner: 4.25 V on USB, 4.20 V without, both full),
// so plugging and unplugging show as steps; a rising trend also means charging, and a
// plateau near 4.2 V while charging means the charger has finished.
enum ChargeState : uint8_t {CHARGE_UNKNOWN=0,CHARGE_OFF,CHARGE_ON,CHARGE_FULL};
constexpr const char* chargeName(uint8_t s){return s==CHARGE_OFF?"discharging":s==CHARGE_ON?"charging":s==CHARGE_FULL?"full":"unknown";}
struct ChargeTracker {
  static constexpr int STEP_MV=30;            // USB plugged or unplugged
  static constexpr int ABOVE_BATTERY_MV=4230; // only external power reads this high
  static constexpr int FULL_MV=4180;
  static constexpr uint32_t TREND_MS=600000;  // rise over 10 minutes means charging
  static constexpr int TREND_MV=15;
  static constexpr uint32_t PLATEAU_MS=600000;// no rise for 10 minutes near 4.2 V: full
  int recent[6]={0};int count=0;              // last trusted samples, oldest first
  int trendMv=0;uint32_t trendAt=0;
  int peakMv=0;uint32_t peakAt=0;
  ChargeState state=CHARGE_UNKNOWN;
  static int median3(const int* v){int a=v[0],b=v[1],c=v[2];return a>b?(b>c?b:(a>c?c:a)):(a>c?a:(b>c?c:b));}
  void set(ChargeState s,uint32_t now,int mv){if(s==CHARGE_ON&&state!=CHARGE_ON){peakMv=mv;peakAt=now;}state=s;}
  // A trusted sample: not during or right after a refresh, a wake or a radio change. single:
  // the one quiet sample of a battery-mode wake (radio still off), compared on its own with
  // the median of the three before it, so USB plugged in or out shows at the next wake.
  static constexpr int SINGLE_STEP_MV=40;
  ChargeState update(uint32_t now,int mv,bool single=false){
    if(count==6){for(int i=0;i<5;i++)recent[i]=recent[i+1];count=5;}
    recent[count++]=mv;
    if(single&&count>=4){
      int d=mv-median3(&recent[count-4]);
      if(d>=SINGLE_STEP_MV||d<=-SINGLE_STEP_MV){
        set(d>0?CHARGE_ON:CHARGE_OFF,now,mv);recent[0]=recent[1]=recent[2]=mv;count=3;trendMv=mv;trendAt=now;
      }
    }
    if(count<3)return state;
    int m=median3(&recent[count-3]);
    if(!trendAt){trendMv=m;trendAt=now;}
    if(count==6){
      int d=m-median3(recent);
      if(d>=STEP_MV){set(CHARGE_ON,now,m);for(int i=0;i<3;i++)recent[i]=recent[i+3];count=3;trendMv=m;trendAt=now;}
      else if(d<=-STEP_MV){set(CHARGE_OFF,now,m);for(int i=0;i<3;i++)recent[i]=recent[i+3];count=3;trendMv=m;trendAt=now;}
    }
    if(m>=ABOVE_BATTERY_MV&&state!=CHARGE_ON&&state!=CHARGE_FULL)set(CHARGE_ON,now,m);
    if(now-trendAt>=TREND_MS){
      int d=m-trendMv;
      if(d>=TREND_MV&&state!=CHARGE_ON&&state!=CHARGE_FULL)set(CHARGE_ON,now,m);
      else if(d<=-TREND_MV&&(state==CHARGE_ON||state==CHARGE_FULL))set(CHARGE_OFF,now,m);
      else if(state==CHARGE_UNKNOWN&&d<TREND_MV)set(CHARGE_OFF,now,m);
      trendMv=m;trendAt=now;
    }
    if(state==CHARGE_ON){
      if(m>peakMv+5){peakMv=m;peakAt=now;}
      else if(m>=FULL_MV&&now-peakAt>=PLATEAU_MS)set(CHARGE_FULL,now,m);
    }
    return state;
  }
};

// The sign's own discharge curve, measured by the CRM from a full cycle: the raw reading
// (before the voltage correction) at 0, 10, ... 100 % of the time the battery lasted.
constexpr int CURVE_POINTS=11;
inline bool validCurve(const int16_t* at){
  if(at[0]<2800||at[CURVE_POINTS-1]>4400)return false;
  for(int i=1;i<CURVE_POINTS;i++)if(at[i]<=at[i-1])return false;
  return true;
}
inline int curvePercent(const int16_t* at,int mv){
  if(mv<=at[0])return 0;
  for(int i=1;i<CURVE_POINTS;i++)if(mv<=at[i])return (i-1)*10+10*(mv-at[i-1])/(at[i]-at[i-1]);
  return 100;
}
// Voltage correction from a multimeter reading, in thousandths (1000 = none).
constexpr bool validScale(int s){return s>=900&&s<=1100;}
