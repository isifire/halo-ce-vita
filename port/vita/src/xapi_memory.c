/* Compiled with the game's headers/ABI, so XDK declarations check wrappers. */
#include <xtl.h>
#include "halo_vita_memory.h"
LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR address, ULONG_PTR alignment, DWORD protect) {
    return halo_vita_alloc(size, alignment, address, protect);
}
VOID WINAPI XPhysicalFree(LPVOID address) { halo_vita_free(address); }
VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD protect) {
    halo_vita_protect(address, size, protect);
}
DWORD WINAPI XQueryMemoryProtect(LPVOID address) { return halo_vita_protection(address); }
