#pragma once
#ifndef AVIOR_RECOVERY
#define AVIOR_RECOVERY 0
#endif
constexpr bool RECOVERY_MODE = AVIOR_RECOVERY != 0;
constexpr const char* FW_VERSION = RECOVERY_MODE ? "0.1.1-recovery" : "0.1.1";
