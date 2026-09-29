#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "halo_vita_memory.h"
#include "cache/physical_memory_map.h"
#include "memory/crc.h"
#include "debugScreen.h"

unsigned int _newlib_heap_size_user = 8u * 1024u * 1024u;
static FILE *log_file;
static int log_error;
static void report(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    psvDebugScreenPrintf("%s", buffer);
    if (log_file && (fputs(buffer, log_file) < 0 || fflush(log_file) != 0)) log_error = 1;
}
/* Matches cseries.h's ARM32 ABI: long=32 bits, boolean=unsigned char. */
void display_assert(char *information, char *file, long line, unsigned char fatal) {
    report("HALO ASSERT fatal=%u %s:%ld %s\n", fatal, file, line, information ? information : "");
}
void system_exit(long code) {
    report("Halo requested exit=%ld\n", code);
    if (log_file) fclose(log_file);
    sceKernelExitProcess((int)code);
    for (;;) {}
}
static int require(int condition, const char *name) {
    report("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}
int main(void) {
    sceIoMkdir("ux0:data/halo-vita-diagnostic", 0777);
    log_file = fopen("ux0:data/halo-vita-diagnostic/core-03.txt", "w");
    if (psvDebugScreenInit() < 0) {
        if (log_file) { fputs("display init failed\n", log_file); fclose(log_file); }
        sceKernelExitProcess(1);
        return 1;
    }
    report("HALO VITA CORE TEST 0.3\n");
    report("Real Halo modules: physical_memory_map.c + crc.c\n");
    report("No map loader, graphics or gameplay yet.\n\n");
    if (!log_file) {
        report("Cannot open core-03.txt. Test skipped.\n");
    } else {
        int rc = halo_vita_memory_init();
        report("arena init=%08lx base=%p\n", (unsigned long)rc, halo_vita_address(0));
        if (rc >= 0) {
            report("Calling Halo physical_memory_allocate...\n");
            physical_memory_allocate();
            report("Calling Halo physical_memory_verify...\n");
            physical_memory_verify();
            void *regions[] = {physical_memory_get_game_state_base_address(),
                physical_memory_get_tag_cache_base_address(),
                physical_memory_get_texture_cache_base_address(),
                physical_memory_get_sound_cache_base_address()};
            const uint32_t sizes[] = {0x1000000,0x1600000,0x1600000,0x400000};
            const char *names[] = {"state", "tags", "textures", "sound"};
            int passed = require(halo_vita_allocation_count() == 4, "four allocations");
            passed &= require(regions[0] == halo_vita_address(HALO_VITA_STATE_OFFSET), "state relocated");
            passed &= require(regions[1] == halo_vita_address(HALO_VITA_TAG_OFFSET), "tags relocated");
            for (unsigned i = 0; i < 4; ++i) {
                uint32_t offset = 0;
                int valid = regions[i] && halo_vita_to_offset(regions[i], &offset) == 0 &&
                    sizes[i] <= HALO_VITA_ARENA_SIZE - offset;
                passed &= require(valid, names[i]);
                report("  address=%p offset=%08lx bytes=%lu\n", regions[i],
                    (unsigned long)offset, (unsigned long)sizes[i]);
                if (valid) {
                    volatile unsigned char *p = regions[i];
                    for (uint32_t n = 0; n < sizes[i]; n += 4096) p[n] = (unsigned char)(0x31 + i);
                    p[sizes[i] - 1] = (unsigned char)(0x61 + i);
                }
            }
            if (passed) {
                for (unsigned i = 0; i < 4; ++i) {
                    volatile unsigned char *p = regions[i];
                    for (uint32_t n = 0; n < sizes[i]; n += 4096)
                        if (p[n] != (unsigned char)(0x31 + i)) passed = 0;
                    if (p[sizes[i] - 1] != (unsigned char)(0x61 + i)) passed = 0;
                }
            }
            passed &= require(passed, "separate cache page sentinels");
            unsigned long crc;
            crc_new(&crc);
            crc_checksum_buffer(&crc, "123456789", 9);
            report("Halo CRC raw=%08lx expected=340bc6d9\n", crc);
            passed &= require(crc == 0x340bc6d9ul, "real Halo CRC vector");
            physical_memory_free();
            passed &= require(halo_vita_allocation_count() == 0, "Halo freed all caches");
            rc = halo_vita_memory_dispose();
            passed &= require(rc >= 0, "Vita arena released");
            report("CORE_TEST=%s\n", passed ? "PASS" : "FAIL");
        } else report("CORE_TEST=FAIL allocation rejected\n");
    }
    report("\nSELECT + START: salir\n");
    report("Informe: ux0:data/halo-vita-diagnostic/core-03.txt\n");
    if (log_error) psvDebugScreenPrintf("ERROR al escribir el informe\n");
    SceCtrlData pad = {0};
    do {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceDisplayWaitVblankStart();
    } while ((pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_START)) != (SCE_CTRL_SELECT | SCE_CTRL_START));
    report("exit=normal\n");
    if (log_file) fclose(log_file);
    sceKernelExitProcess(0);
    return 0;
}
