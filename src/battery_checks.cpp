#include "battery.h"
static_assert(batteryPercent(4250)==100&&batteryPercent(4200)==100,"full");
static_assert(batteryPercent(3300)==0&&batteryPercent(3000)==0,"empty");
static_assert(batteryPercent(3800)==50&&batteryPercent(3950)==71,"interpolated");
static_assert(plausibleBattery(1900)&&!plausibleBattery(1400)&&!plausibleBattery(2300)&&!plausibleBattery(0),"Li-ion window behind a 1:2 divider");
