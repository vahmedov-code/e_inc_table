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
