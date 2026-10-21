#include "render_trace.h"
static_assert(!blocksAutoRender(PHASE_IDLE), "A finished render must not block boot");
static_assert(blocksAutoRender(PHASE_REFRESH) && blocksAutoRender(PHASE_START), "An interrupted render must block auto retry");
static_assert(phaseName(PHASE_REFRESH)[0]=='r' && phaseName(PHASE_COUNT)[0]=='i', "Phase names");
