#ifndef PTUBE_POWER_H
#define PTUBE_POWER_H
#include <stdbool.h>
static inline int ptube_power_cpu(unsigned mode, bool playback, bool busy)
{
    if (busy || mode == 0 || mode > 2) return 333;
    if (playback) return mode == 1 ? 266 : 222;
    return mode == 1 ? 222 : 111;
}
#endif
