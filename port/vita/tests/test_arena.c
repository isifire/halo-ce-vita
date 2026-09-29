#include "halo_vita_memory.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int halo_vita_arena_bind(void *base);
int halo_vita_arena_unbind(void);
int main(void) {
    unsigned char *raw = malloc(HALO_VITA_ARENA_SIZE + 4096);
    assert(raw);
    void *base = (void *)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095);
    assert(halo_vita_arena_bind(base) == 0);
    assert(!halo_vita_alloc(0, 0, 0, 4));
    assert(!halo_vita_alloc(UINT32_MAX, 0, 0, 4));
    assert(!halo_vita_alloc(4096, 3, 0, 4));
    assert(!halo_vita_alloc(4096, 0, 1, 4));
    assert(!halo_vita_alloc(8192, 0, HALO_VITA_ARENA_SIZE - 4096, 4));
    assert(!halo_vita_alloc(4096, 0, 0, 2));
    void *state = halo_vita_alloc(0x1000000, 0, HALO_VITA_STATE_OFFSET, 4);
    void *tags = halo_vita_alloc(0x1600000, 0, HALO_VITA_TAG_OFFSET, 4);
    void *textures = halo_vita_alloc(0x1600000, 0, UINT32_MAX, 0x404);
    void *sounds = halo_vita_alloc(0x400000, 0, UINT32_MAX, 4);
    assert(state && tags && textures && sounds);
    assert(state == halo_vita_address(HALO_VITA_STATE_OFFSET));
    assert(tags == halo_vita_address(HALO_VITA_TAG_OFFSET));
    assert(halo_vita_allocation_count() == 4);
    assert(!halo_vita_alloc(4096, 0, HALO_VITA_STATE_OFFSET, 4));
    assert(halo_vita_free((unsigned char *)state + 4096) < 0);
    assert(halo_vita_free((unsigned char *)state + 1) < 0);
    assert(halo_vita_arena_unbind() < 0);
    uint32_t offset;
    assert(halo_vita_to_offset(tags, &offset) == 0 && offset == HALO_VITA_TAG_OFFSET);
    assert(halo_vita_to_offset((unsigned char *)base + HALO_VITA_ARENA_SIZE, &offset) < 0);
    assert(halo_vita_protection((unsigned char *)state + 0xffffff) == 4);
    assert(halo_vita_protect(tags, 0x1600000, 2) == 0);
    assert(halo_vita_protection(tags) == 2);
    assert(halo_vita_protection((unsigned char *)tags + 0x15fffff) == 2);
    assert(halo_vita_protect((unsigned char *)tags + 1, 4096, 4) < 0);
    assert(halo_vita_protect(tags, 0, 4) < 0);
    assert(halo_vita_protect(tags, 0x1600000, 4) == 0);
    assert(halo_vita_free(state) == 0 && halo_vita_free(state) < 0);
    assert(halo_vita_protection(state) == 0);
    assert(halo_vita_free(tags) == 0);
    assert(halo_vita_free(textures) == 0);
    assert(halo_vita_free(sounds) == 0);
    void *whole = halo_vita_alloc(HALO_VITA_ARENA_SIZE, 0, 0, 4);
    assert(whole == base && !halo_vita_alloc(4096, 0, UINT32_MAX, 4));
    assert(halo_vita_free(whole) == 0);
    void *aligned = halo_vita_alloc(4096, 65536, UINT32_MAX, 4);
    assert(aligned && (uintptr_t)aligned % 65536 == 0);
    assert(halo_vita_free(aligned) == 0);
    assert(halo_vita_arena_unbind() == 0);
    assert(!halo_vita_address(0));
    free(raw);
    puts("arena tests PASS: bounds, overlaps, alignment, offsets, free, exhaustion");
    return 0;
}
