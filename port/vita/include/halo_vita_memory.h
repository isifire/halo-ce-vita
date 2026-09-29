#ifndef HALO_VITA_MEMORY_H
#define HALO_VITA_MEMORY_H
#include <stdint.h>
#include <stddef.h>
#define HALO_VITA_ARENA_SIZE 0x08000000u
#define HALO_VITA_TAG_OFFSET 0x003a6000u
#define HALO_VITA_STATE_OFFSET 0x01a00000u
/* Offset translation only. Does not relocate pointers embedded in map files. */
int halo_vita_memory_init(void);
int halo_vita_memory_dispose(void);
void *halo_vita_address(uint32_t offset);
void *halo_vita_alloc(uint32_t bytes, uint32_t alignment, uint32_t offset, uint32_t protect);
int halo_vita_free(void *address);
uint32_t halo_vita_protection(const void *address);
int halo_vita_protect(void *address, uint32_t bytes, uint32_t protect);
int halo_vita_to_offset(const void *address, uint32_t *offset);
unsigned halo_vita_allocation_count(void);
#endif
