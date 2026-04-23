#include <stddef.h>
#include <stdint.h>

#include "heatshrink_config.h"

#if HEATSHRINK_DYNAMIC_ALLOC
static uint8_t avr_cycle_heap[4096];
static size_t avr_cycle_heap_used;

void avr_cycle_alloc_reset(void) {
    avr_cycle_heap_used = 0;
}

void *avr_cycle_malloc(size_t sz) {
    size_t align = sizeof(size_t) - 1u;
    size_t aligned = (sz + align) & ~align;
    if (avr_cycle_heap_used + aligned > sizeof(avr_cycle_heap)) {
        return NULL;
    }
    void *ptr = &avr_cycle_heap[avr_cycle_heap_used];
    avr_cycle_heap_used += aligned;
    return ptr;
}

void avr_cycle_free(void *p, size_t sz) {
    (void)p;
    (void)sz;
}
#endif
