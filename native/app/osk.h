/* Adapted from KOMI Tube (MIT), copyright (c) 2026 komi-tube contributors.
 * PTube changes: see native/UPSTREAM.md. Licence: native/LICENSE-KOMI. */
#ifndef KOMI_OSK_H
#define KOMI_OSK_H

#include <stdbool.h>
#include <stddef.h>

/* Show the firmware keyboard (blocking) and return the text as UTF-8.
   False when the user cancelled. */
bool osk_input(const char *description, const char *initial, char *output,
               size_t capacity);

#endif
