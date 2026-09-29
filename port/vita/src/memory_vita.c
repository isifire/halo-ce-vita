#include <psp2/kernel/sysmem.h>
#include "halo_vita_memory.h"
int halo_vita_arena_bind(void *base);
int halo_vita_arena_unbind(void);
static SceUID block = -1;
int halo_vita_memory_init(void) {
    if (block >= 0) return -1;
    block = sceKernelAllocMemBlock("halo_arena", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                  HALO_VITA_ARENA_SIZE, NULL);
    if (block < 0) return block;
    void *base = NULL;
    int rc = sceKernelGetMemBlockBase(block, &base);
    if (rc >= 0) rc = halo_vita_arena_bind(base);
    if (rc < 0) { sceKernelFreeMemBlock(block); block = -1; }
    return rc;
}
int halo_vita_memory_dispose(void) {
    if (block < 0 || halo_vita_allocation_count()) return -1;
    int rc = sceKernelFreeMemBlock(block);
    if (rc >= 0) { block = -1; halo_vita_arena_unbind(); }
    return rc;
}
