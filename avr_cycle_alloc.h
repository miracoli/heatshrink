#ifndef AVR_CYCLE_ALLOC_H
#define AVR_CYCLE_ALLOC_H

#include <stddef.h>

void avr_cycle_alloc_reset(void);
void *avr_cycle_malloc(size_t sz);
void avr_cycle_free(void *p, size_t sz);

#endif
