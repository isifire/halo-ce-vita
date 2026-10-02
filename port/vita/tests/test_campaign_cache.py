"""Host regression tests of production cache selection/completion and Vita diagnostics.

Does not emulate loading a real map or the Vita filesystem/GPU.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_texture_layout import definition, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    cache = (ROOT / 'source/cache/cache_files_windows.c').read_text()
    decompressor = (ROOT / 'source/cache/cache_files_decompress_windows.c').read_text()
    walker = (ROOT / 'source/cseries/stack_walk_windows.c').read_text()
    # Select definitions, not the forward declarations.
    lookup = definition(cache, 'static short cached_map_files_find_map(\n\tconst char *map_name)\n{')
    status = definition(decompressor, 'short cache_copy_get_status(')
    stack = definition(walker, 'void stack_walk_with_context(')
    harness = r'''
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <stdint.h>
#define HALO_VITA 1
#define TRUE 1
#define FALSE 0
#define NONE -1
#define NUMBER_OF_CACHED_MAP_FILES 3
#define CACHE_FILE_HEADER_SIGNATURE 0x68656164u
#define CACHE_FILE_FOOTER_SIGNATURE 0x666f6f74u
#define _stricmp strcasecmp
typedef int boolean;
typedef float real;
struct cache_file_header { unsigned header_signature, footer_signature; char name[32]; };
struct cached_map_file { struct cache_file_header header; };
static struct cached_map_file slots[3];
static struct cached_map_file *cached_map_file_get(short i) { assert(i>=0&&i<3); return &slots[i]; }
enum { _copy_read_failed_bit, _copy_bad_file_bit, _copy_write_failed_bit };
enum { _cache_copy_bad_file_failure, _cache_copy_read_failure, _cache_copy_write_failure,
       _cache_copy_in_progress, _cache_copy_finished };
#define TEST_FLAG(f,b) ((f)&(1u<<(b)))
#define match_assert(f,l,x) assert(x)
struct copy { unsigned flags; int blocking, copy_thread, copy_complete_event, progress_update_event;
              struct { int size; } header; float read_progress; } state, *global_self=&state;
static int finished, fail_during_wait, fail_during_sleep;
static unsigned long cache_copy_get_flags(void) { return state.flags; }
static void Sleep(int n) { (void)n; if(fail_during_sleep) { finished=1; state.flags=1u<<_copy_write_failed_bit; } }
static int WaitForSingleObject(int e, int timeout) {
    (void)timeout;
    if(e==state.copy_complete_event) {
        if(fail_during_wait) { finished=1; state.flags=1u<<_copy_bad_file_bit; }
        return finished?0:258;
    }
    return 0;
}
typedef void CONTEXT;
enum { _error_silent };
static int logged;
static void error(int level, const char *message) { (void)level; assert(strstr(message,"x86")); logged++; }
'''
    harness += lookup + '\n' + status + '\n' + stack
    harness += r'''
static void reset_copy(void) {
    memset(&state,0,sizeof(state)); state.copy_thread=1; state.copy_complete_event=1;
    state.progress_update_event=2; state.header.size=8192; state.read_progress=0.5f;
    finished=fail_during_wait=fail_during_sleep=0;
}
int main(void) {
    float progress=0;
    assert(cached_map_files_find_map(NULL)==NONE);
    assert(cached_map_files_find_map("")==NONE);
    assert(cached_map_files_find_map("a10")==NONE);
    strcpy(slots[0].header.name,"a10"); /* name alone is insufficient */
    assert(cached_map_files_find_map("a10")==NONE);
    slots[1].header.header_signature=CACHE_FILE_HEADER_SIGNATURE;
    slots[1].header.footer_signature=CACHE_FILE_FOOTER_SIGNATURE;
    strcpy(slots[1].header.name,"a10");
    assert(cached_map_files_find_map("A10")==1);
    memset(slots[1].header.name,'a',32);
    assert(cached_map_files_find_map("a10")==NONE);
    reset_copy(); assert(cache_copy_get_status(&progress)==_cache_copy_in_progress); assert(progress==0.5f);
    finished=1; assert(cache_copy_get_status(&progress)==_cache_copy_finished);
    reset_copy(); state.blocking=1; fail_during_sleep=1;
    assert(cache_copy_get_status(&progress)==_cache_copy_write_failure);
    reset_copy(); fail_during_wait=1;
    assert(cache_copy_get_status(&progress)==_cache_copy_bad_file_failure);
    /* A deliberately invalid XDK context must not be dereferenced on ARM. */
    stack_walk_with_context(NULL,0,(CONTEXT *)(uintptr_t)1); assert(logged==1);
    FILE *output=tmpfile(); assert(output);
    stack_walk_with_context(output,0,(CONTEXT *)(uintptr_t)1);
    rewind(output); char line[128]; assert(fgets(line,sizeof(line),output)); assert(strstr(line,"x86")); fclose(output);
    puts("campaign cache lookup/completion and ARM diagnostic regressions passed");
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'campaign_cache.c'
        exe = Path(tmp) / 'campaign_cache.exe'
        src.write_text(harness)
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
