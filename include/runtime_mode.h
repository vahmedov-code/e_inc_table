#pragma once
#ifndef AVIOR_RECOVERY
#define AVIOR_RECOVERY 0
#endif
#ifndef AVIOR_RECOVERY_WIFI
#define AVIOR_RECOVERY_WIFI 0
#endif
#if AVIOR_RECOVERY_WIFI && !AVIOR_RECOVERY
#error "Recovery Wi-Fi must also disable the display with AVIOR_RECOVERY=1"
#endif
constexpr bool RECOVERY_MODE = AVIOR_RECOVERY != 0;
constexpr bool RECOVERY_WIFI = RECOVERY_MODE && AVIOR_RECOVERY_WIFI != 0;
constexpr bool canMutate(bool wifi=false){return !RECOVERY_MODE || (RECOVERY_WIFI && wifi);}
constexpr const char* MODE_NAME = RECOVERY_WIFI ? "recovery-wifi" : RECOVERY_MODE ? "recovery" : "normal";
constexpr const char* FW_VERSION = RECOVERY_WIFI ? "0.2.0-recovery-wifi" : RECOVERY_MODE ? "0.2.0-recovery" : "0.2.0";
