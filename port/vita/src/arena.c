/* Single-threaded bring-up allocator. No GPU mapping or page fault tracking. */
#include "halo_vita_memory.h"
#include <string.h>
#define PAGE 4096u
#define PAGES (HALO_VITA_ARENA_SIZE / PAGE)
static unsigned char *arena;
static unsigned char occupied[PAGES];
static uint32_t lengths[PAGES];
static unsigned count;

int halo_vita_arena_bind(void *base) {
    if (arena || !base || (uintptr_t)base % PAGE) return -1;
    arena = base;
    memset(occupied, 0, sizeof(occupied));
    memset(lengths, 0, sizeof(lengths));
    count = 0;
    return 0;
}
int halo_vita_arena_unbind(void) {
    if (count) return -1;
    arena = NULL;
    return 0;
}
void *halo_vita_address(uint32_t offset) {
    return arena && offset < HALO_VITA_ARENA_SIZE ? arena + offset : NULL;
}
int halo_vita_to_offset(const void *address, uint32_t *offset) {
    uintptr_t p = (uintptr_t)address, b = (uintptr_t)arena;
    if (!arena || !offset || p < b || p - b >= HALO_VITA_ARENA_SIZE) return -1;
    *offset = (uint32_t)(p - b);
    return 0;
}
static int available(uint32_t first, uint32_t pages) {
    if (first >= PAGES || pages > PAGES - first) return 0;
    for (uint32_t i = first; i < first + pages; ++i)
        if (occupied[i]) return 0;
    return 1;
}
void *halo_vita_alloc(uint32_t bytes, uint32_t alignment, uint32_t offset, uint32_t protect) {
    /* Caches request WRITECOMBINE, but this CPU prototype uses ordinary RW
     * storage. Reject actual read-only/executable protection requests. */
    if (!arena || !bytes || bytes > HALO_VITA_ARENA_SIZE ||
        (protect & ~0x400u) != 4u) return NULL;
    if (!alignment) alignment = PAGE;
    if ((alignment & (alignment - 1)) || alignment > HALO_VITA_ARENA_SIZE) return NULL;
    if (alignment < PAGE) alignment = PAGE;
    uint32_t pages = (bytes + PAGE - 1) / PAGE, first = PAGES;
    if (offset != UINT32_MAX) {
        if (offset % PAGE || offset >= HALO_VITA_ARENA_SIZE ||
            ((uintptr_t)arena + offset) % alignment) return NULL;
        if (available(offset / PAGE, pages)) first = offset / PAGE;
    } else {
        for (uint32_t n = PAGES - pages + 1; n > 0; --n) {
            uint32_t candidate = n - 1;
            if (((uintptr_t)arena + candidate * PAGE) % alignment == 0 &&
                available(candidate, pages)) { first = candidate; break; }
        }
    }
    if (first == PAGES) return NULL;
    for (uint32_t i = first; i < first + pages; ++i) occupied[i] = (unsigned char)(protect & ~0x400u);
    lengths[first] = pages;
    ++count;
    void *result = arena + first * PAGE;
    memset(result, 0, pages * PAGE);
    return result;
}
int halo_vita_free(void *address) {
    uint32_t offset;
    if (halo_vita_to_offset(address, &offset) < 0 || offset % PAGE) return -1;
    uint32_t first = offset / PAGE, pages = lengths[first];
    if (!pages) return -1;
    for (uint32_t i = first; i < first + pages; ++i) occupied[i] = 0;
    lengths[first] = 0;
    --count;
    return 0;
}
uint32_t halo_vita_protection(const void *address) {
    uint32_t offset;
    return halo_vita_to_offset(address, &offset) == 0 ? occupied[offset / PAGE] : 0u;
}
int halo_vita_protect(void *address, uint32_t bytes, uint32_t protect) {
    uint32_t offset, i;
    protect &= ~0x400u;
    if (!bytes || halo_vita_to_offset(address, &offset) < 0 ||
        offset % PAGE || (protect != 2u && protect != 4u)) return -1;
    uint32_t pages = (bytes + PAGE - 1) / PAGE;
    uint32_t first = offset / PAGE;
    if (first >= PAGES || pages > PAGES - first) return -1;
    for (i = first; i < first + pages; ++i)
        if (!occupied[i]) return -1;
    /* CPU bring-up tracks the Xbox protection contract. Vita page permissions
     * are unchanged until a safe kernel API implementation is proven. */
    for (i = first; i < first + pages; ++i) occupied[i] = (unsigned char)protect;
    return 0;
}
unsigned halo_vita_allocation_count(void) { return count; }
