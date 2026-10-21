#include "schedule.h"
// Executed by the compiler against the actual scheduler, with no runtime overhead.
constexpr Event week[]={{1,540,0},{1,1080,1},{5,1080,2}};
static_assert(scheduledStatus(week,0,1,600)==-1,"empty schedule");
static_assert(scheduledStatus(week,3,1,539)==2,"previous week wraps");
static_assert(scheduledStatus(week,3,1,540)==0,"opening boundary inclusive");
static_assert(scheduledStatus(week,3,1,1079)==0,"before close");
static_assert(scheduledStatus(week,3,1,1080)==1,"closing boundary inclusive");
static_assert(scheduledStatus(week,3,2,0)==1,"overnight persistence");
static_assert(scheduledStatus(week,3,5,1080)==2,"Friday boundary");
static_assert(scheduledStatus(week,3,0,0)==2,"Sunday boundary");
constexpr Event unsorted[]={{1,1080,1},{1,540,0}};
static_assert(scheduledStatus(unsorted,2,1,600)==0,"order independent");
