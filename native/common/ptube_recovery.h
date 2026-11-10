#ifndef PTUBE_RECOVERY_H
#define PTUBE_RECOVERY_H
#include <stdbool.h>
#include <stdint.h>

/* One fresh transport per play selection; never loop on a broken network. */
static inline bool ptube_recovery_allowed(unsigned attempts, uint64_t position)
{
    return attempts == 0 && position > 0;
}
static inline bool ptube_refresh_stalled(uint64_t now, uint64_t started)
{
    return started != 0 && now - started >= UINT64_C(20000000);
}
#endif
