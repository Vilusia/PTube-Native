#include <assert.h>
#include <stdio.h>
#include "../common/ptube_power.h"
int main(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        assert(ptube_power_cpu(mode, false, true) == 333);
        assert(ptube_power_cpu(mode, true, true) == 333);
        assert(ptube_power_cpu(mode, true, false) >= 222);
        assert(ptube_power_cpu(mode, false, false) <= ptube_power_cpu(mode, true, false));
    }
    assert(ptube_power_cpu(0, false, false) == 333);
    assert(ptube_power_cpu(1, true, false) == 266);
    assert(ptube_power_cpu(2, true, false) == 222);
    assert(ptube_power_cpu(2, false, false) == 111);
    assert(ptube_power_cpu(99, false, false) == 333);
    puts("Power policy tests: PASS");
}
