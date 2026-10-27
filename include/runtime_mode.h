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
#define AVIOR_VERSION "0.4.9"
#if AVIOR_RECOVERY_WIFI
#define AVIOR_VERSION_FULL AVIOR_VERSION "-recovery-wifi"
#elif AVIOR_RECOVERY
#define AVIOR_VERSION_FULL AVIOR_VERSION "-recovery"
#else
#define AVIOR_VERSION_FULL AVIOR_VERSION
#endif
constexpr const char* FW_VERSION = AVIOR_VERSION_FULL;
// Found by the CRM in an uploaded firmware.bin: which version it is, and that it is AviorSign.
#define AVIOR_FW_MARKER "AVIORSIGN_VERSION=" AVIOR_VERSION_FULL ";"
