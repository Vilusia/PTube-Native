#include "discover.h"

static const char *const topics[] = {
    "science explained", "retro gaming", "live music performance",
    "space exploration", "nature documentary", "cooking recipes",
    "technology explained", "comedy sketches", "animated short film",
    "travel destinations", "restoration projects", "history documentary"
};
size_t discover_topic_count(void) {return sizeof topics / sizeof topics[0];}
unsigned discover_pick(uint32_t seed, int previous)
{
    unsigned count = (unsigned)discover_topic_count();
    if (previous < 0 || (unsigned)previous >= count) return seed % count;
    unsigned next = seed % (count - 1u);
    return next >= (unsigned)previous ? next + 1u : next;
}
const char *discover_topic(unsigned index)
{
    return topics[index % discover_topic_count()];
}
void discover_shuffle(YtVideo *videos, size_t count, uint32_t seed)
{
    if (videos == NULL || count < 2) return;
    for (size_t remaining = count; remaining > 1; remaining--) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        size_t other = seed % remaining;
        YtVideo saved = videos[remaining-1];
        videos[remaining-1] = videos[other];
        videos[other] = saved;
    }
}
