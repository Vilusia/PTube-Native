#include <assert.h>
#include <stdio.h>
#include "../common/ptube_recovery.h"
int main(void)
{
    assert(!ptube_recovery_allowed(0, 0));
    assert(ptube_recovery_allowed(0, 234981188));
    assert(!ptube_recovery_allowed(1, 234981188));
    assert(!ptube_recovery_allowed(2, 234981188));
    assert(!ptube_refresh_stalled(25000000, 0));
    assert(!ptube_refresh_stalled(20999999, 1000000));
    assert(ptube_refresh_stalled(21000000, 1000000));
    assert(ptube_refresh_stalled(22000000, 1000000));
    puts("Recovery policy tests: PASS");
}
