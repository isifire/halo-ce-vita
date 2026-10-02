"""Exercise production save-container creation against a temporary host disk.

Only Vita path/syscall adapters are substituted; this is not a hardware test.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_texture_layout import definition, ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (ROOT / 'port/vita/src/xapi_platform.c').read_text()
harness = r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <direct.h>
typedef const char *LPCSTR;
typedef const unsigned short *LPCWSTR;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef struct stat SceIoStat;
#define VITA_PATH_MAX 512
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define ERROR_SUCCESS 0
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_GEN_FAILURE 31
static int sceIoGetstat(const char *path, SceIoStat *s) { return stat(path,s); }
#define sceIoMkdir(path, mode) _mkdir(path)
#define sceIoRmdir _rmdir
#define sceIoRemove remove
#define sceIoRename rename
static unsigned long long sceKernelGetProcessTimeWide(void) { static unsigned long long n; return ++n; }
static void translate_path(const char *guest, char *native, unsigned size) {
    const char *tail = guest + 2;
    while (*tail == '\\' || *tail == '/') ++tail;
    snprintf(native, size, "saves/%s", tail);
    for (char *p = native; *p; ++p) if (*p == '\\') *p = '/';
    size_t n = strlen(native);
    while (n && native[n-1] == '/') native[--n] = 0;
}
'''
harness += definition(source, 'static int save_path(')
harness += definition(source, 'static unsigned int save_name_hash(')
harness += definition(source, 'static DWORD vita_create_save_game(')
harness += r'''
int main(void) {
    const unsigned short a[] = {'A',0}, b[] = {'B',0}, empty[] = {0};
    char path[260], again[260], native[512], metadata[512];
    assert(vita_create_save_game("u:\\", a, OPEN_EXISTING,0,path,sizeof(path)) == ERROR_FILE_NOT_FOUND);
    assert(vita_create_save_game("u:\\", a, CREATE_NEW,0,path,sizeof(path)) == 0);
    assert(path[strlen(path)-1] == '\\');
    /* A metadata-only interrupted attempt is retired and can be retried. */
    assert(vita_create_save_game("u:\\", a, CREATE_NEW,0,again,sizeof(again)) == 0);
    translate_path(again,native,sizeof(native));
    snprintf(metadata,sizeof(metadata),"%s/blam.sav",native);
    FILE *content = fopen(metadata,"wb"); assert(content); fclose(content);
    assert(vita_create_save_game("u:\\", a, CREATE_NEW,0,again,sizeof(again)) == ERROR_ALREADY_EXISTS);
    assert(vita_create_save_game("u:\\", a, OPEN_EXISTING,0,again,sizeof(again)) == 0);
    assert(!strcmp(path,again));
    assert(vita_create_save_game("u:\\", b, OPEN_ALWAYS,0,again,sizeof(again)) == 0);
    assert(strcmp(path,again));
    assert(vita_create_save_game(NULL, b, OPEN_EXISTING,0,again,sizeof(again)) == 0);
    assert(vita_create_save_game("u:\\", empty,CREATE_NEW,0,again,sizeof(again)) == ERROR_INVALID_PARAMETER);
    assert(vita_create_save_game("u:\\", a,CREATE_NEW,0,again,2) == ERROR_INVALID_PARAMETER);
    translate_path(path,native,sizeof(native));
    snprintf(metadata,sizeof(metadata),"%s/name.bin",native);
    FILE *f = fopen(metadata,"rb"); assert(f);
    unsigned short stored[128]; assert(fread(stored,1,sizeof(stored),f) == sizeof(stored)); fclose(f);
    assert(stored[0] == 'A' && stored[1] == 0);
    puts("save create/reopen/duplicate/metadata tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    c = directory / 'save_test.c'
    c.write_text(harness)
    exe = directory / 'save_test.exe'
    subprocess.run([args.cc, '-std=c11', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=directory, check=True)
