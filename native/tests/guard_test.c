#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../common/ptube_guard.h"

int main(void)
{
    assert(!ptube_deadline_reached(1000u, 46000u));
    assert(!ptube_deadline_reached(45999u, 46000u));
    assert(ptube_deadline_reached(46000u, 46000u));
    assert(ptube_deadline_reached(47000u, 46000u));
    /* A search can start before the millisecond clock wraps. */
    uint32_t start = UINT32_MAX - 1000u;
    uint32_t deadline = start + 45000u;
    assert(!ptube_deadline_reached(start, deadline));
    assert(!ptube_deadline_reached(100u, deadline));
    assert(!ptube_deadline_reached(deadline-1u, deadline));
    assert(ptube_deadline_reached(deadline, deadline));
    assert(ptube_deadline_reached(deadline+1u, deadline));
    puts("Operation deadline tests: PASS");
    return 0;
}
