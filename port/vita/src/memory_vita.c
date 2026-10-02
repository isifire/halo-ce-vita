#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include "halo_vita_memory.h"

unsigned int _newlib_heap_size_user = 32u * 1024u * 1024u;
unsigned int sceUserMainThreadStackSize = 2u * 1024u * 1024u;

int halo_vita_arena_bind(void *base);
int halo_vita_arena_unbind(void);
static SceUID block = -1;

__attribute__((constructor(101)))
int halo_vita_memory_init(void) {
    if (block >= 0) return 0;
    sceIoMkdir("ux0:data/halo", 0777);
    /* Start each launch with a clean trace so an old failure cannot be
     * mistaken for the current boot. */
    FILE *f = fopen("ux0:data/halo/boot.log", "w");
    if (f) { fprintf(f, "=== Halo CE PS Vita Booting ===\nhalo_vita_memory_init: allocating arena (128 MB)...\n"); fclose(f); }

    block = sceKernelAllocMemBlock("halo_arena", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                  HALO_VITA_ARENA_SIZE, NULL);
    if (block < 0) {
        f = fopen("ux0:data/halo/boot.log", "a");
        if (f) { fprintf(f, "halo_vita_memory_init: FAILED to allocate arena: 0x%08X\n", (unsigned int)block); fclose(f); }
        return block;
    }
    void *base = NULL;
    int rc = sceKernelGetMemBlockBase(block, &base);
    if (rc >= 0) rc = halo_vita_arena_bind(base);
    if (rc < 0) { sceKernelFreeMemBlock(block); block = -1; }

    f = fopen("ux0:data/halo/boot.log", "a");
    if (f) { fprintf(f, "halo_vita_memory_init: SUCCESS, arena base = %p, rc = %d\n", base, rc); fclose(f); }
    return rc;
}
int halo_vita_memory_dispose(void) {
    if (block < 0 || halo_vita_allocation_count()) return -1;
    int rc = sceKernelFreeMemBlock(block);
    if (rc >= 0) { block = -1; halo_vita_arena_unbind(); }
    return rc;
}
