#include "runtime_mode.h"
#if AVIOR_RECOVERY_WIFI
static_assert(RECOVERY_MODE, "Wi-Fi diagnostics must exclude the display task");
static_assert(canMutate(true), "Wi-Fi setup must be available");
static_assert(!canMutate(), "Display, storage and other mutations must remain blocked");
#elif AVIOR_RECOVERY
static_assert(!canMutate(true), "AP-only recovery must block credential changes");
static_assert(!canMutate(), "AP-only recovery must block every mutation");
#else
static_assert(canMutate() && canMutate(true), "Normal mode must retain controls");
#endif
