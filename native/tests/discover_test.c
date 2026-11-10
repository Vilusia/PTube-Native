#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../app/discover.h"
int main(void)
{
    unsigned count = (unsigned)discover_topic_count();
    assert(count >= 2);
    for (unsigned previous=0; previous<count; previous++) {
        unsigned seen[32] = {0};
        assert(count <= 32);
        for (unsigned seed=0;seed<256;seed++) {
            unsigned next = discover_pick(seed, (int)previous);
            assert(next < count && next != previous);
            assert(discover_topic(next)[0]);
            seen[next] = 1;
        }
        for (unsigned topic=0;topic<count;topic++) if (topic != previous) assert(seen[topic]);
    }
    YtVideo videos[20] = {0};
    for (unsigned i=0;i<20;i++) snprintf(videos[i].id, sizeof videos[i].id, "%011u", i);
    discover_shuffle(NULL, 0, 0);
    discover_shuffle(videos, 20, 12345);
    unsigned seen[20] = {0};
    int moved = 0;
    for (unsigned i=0;i<20;i++) {
        unsigned id;
        assert(sscanf(videos[i].id, "%u", &id) == 1 && id < 20);
        assert(!seen[id]++);
        moved |= id != i;
    }
    assert(moved);
    puts("Discovery tests: PASS (non-repeating topics, full result permutation)");
}
