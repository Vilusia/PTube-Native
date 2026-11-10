#ifndef PTUBE_DISCOVER_H
#define PTUBE_DISCOVER_H
#include <stddef.h>
#include <stdint.h>
#include "yt_results.h"
size_t discover_topic_count(void);
unsigned discover_pick(uint32_t seed, int previous);
const char *discover_topic(unsigned index);
void discover_shuffle(YtVideo *videos, size_t count, uint32_t seed);
#endif
