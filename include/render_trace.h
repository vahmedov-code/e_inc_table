#pragma once
#include <stdint.h>

// Last display stage, persisted in NVS before each step. A nonzero value at
// boot means the previous refresh never finished: the MCU was reset mid-way.
enum RenderPhase : uint8_t {
  PHASE_IDLE=0,PHASE_START,PHASE_RESET,PHASE_POWER_ON,PHASE_FRAME,PHASE_REFRESH,PHASE_POWER_OFF,PHASE_SLEEP,PHASE_COUNT
};
constexpr const char* phaseName(uint8_t p){
  return p==PHASE_IDLE?"idle":p==PHASE_START?"start":p==PHASE_RESET?"reset":p==PHASE_POWER_ON?"power_on":
    p==PHASE_FRAME?"frame":p==PHASE_REFRESH?"refresh":p==PHASE_POWER_OFF?"power_off":p==PHASE_SLEEP?"deep_sleep":"invalid";
}
// Interrupted render blocks the automatic retry after boot; only a manual
// request from the panel clears it, so a reset during refresh cannot loop.
constexpr bool blocksAutoRender(uint8_t interrupted){return interrupted!=PHASE_IDLE;}
