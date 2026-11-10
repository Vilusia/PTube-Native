#ifndef PTUBE_GUARD_H
#define PTUBE_GUARD_H
#include <stdbool.h>
#include <stdint.h>

/* All operation windows are shorter than 2^31 ms; unsigned subtraction
   handles the PSP's truncated millisecond clock wrapping after 49 days. */
static inline bool ptube_deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
void ptube_operation_begin(unsigned seconds);
void ptube_operation_end(void);
void ptube_set_back_button(uint32_t button);
#endif
