"""Run the production profile-name probes against real save-container logic."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_texture_layout import ROOT, definition


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()

    platform_source = (ROOT / 'port/vita/src/xapi_platform.c').read_text()
    save_source = (ROOT / 'source/saved games/saved_game_files.c').read_text()
    harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef const char *LPCSTR;
typedef const wchar_t *LPCWSTR;
typedef int boolean;
#define WINAPI
#define VITA_PATH_MAX 512
#define MAXIMUM_FILENAME_LENGTH 260
#define MEMORY_UNIT_ROOT_PATH_SIZE 260
#define MAX_GAMENAME 64
#define MAXIMUM_UNTITLED_SAVED_GAMES 8
#define _memory_unit_hard_drive 0
#define _error_silent 0
#define UNICODE_STRING_LIST_TAG 1
#define _saved_game_file_string_untitled_name_format 2
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define ERROR_SUCCESS 0
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_GEN_FAILURE 31
#define TRUE 1
#define FALSE 0
#define NONE -1
#define match_assert(file, line, expression) assert(expression)
typedef struct stat SceIoStat;
#include <sys/stat.h>
#include <direct.h>
static wchar_t memory_unit_root_path[1][MEMORY_UNIT_ROOT_PATH_SIZE] = { L"u:\\" };
static void error(int level, const char *format, ...) { (void)level; (void)format; }
static char *wide_to_ascii(const wchar_t *source, char *destination, size_t capacity) {
    size_t i = 0;
    if (!source || !destination || !capacity) return NULL;
    while (source[i] && i + 1 < capacity) { destination[i] = (char)source[i]; i++; }
    destination[i] = 0;
    return destination;
}
static long tag_loaded(long tag, const char *path) { (void)tag; (void)path; return 1; }
static const wchar_t *unicode_string_list_get_string(long index, long item) {
    (void)index; (void)item; return L"Untitled %d";
}
static int usnprintf(wchar_t *buffer, size_t count, const wchar_t *format, int value) {
    return swprintf(buffer, count, format, value);
}
static int sceIoGetstat(const char *path, SceIoStat *stat_buffer) { return stat(path, stat_buffer); }
#define sceIoMkdir(path, mode) _mkdir(path)
#define sceIoRmdir _rmdir
#define sceIoRemove remove
#define sceIoRename rename
static unsigned long long sceKernelGetProcessTimeWide(void) { static unsigned long long tick; return ++tick; }
static void translate_path(const char *guest, char *native, unsigned size) {
    const char *tail = guest + 2;
    while (*tail == '\\' || *tail == '/') ++tail;
    snprintf(native, size, "saves/%s", tail);
    for (char *p = native; *p; ++p) if (*p == '\\') *p = '/';
    size_t n = strlen(native);
    while (n && native[n - 1] == '/') native[--n] = 0;
}
'''
    for marker in (
        'static int save_path(',
        'static unsigned int save_name_hash(',
        'static DWORD vita_create_save_game(',
    ):
        harness += definition(platform_source, marker) + '\n'
    harness += r'''
DWORD WINAPI XCreateSaveGame(LPCSTR root, LPCWSTR name, DWORD disposition,
    DWORD flags, char *path, UINT path_size) {
    return vita_create_save_game(root, name, disposition, flags, path, path_size);
}
'''
    for marker in (
        'boolean saved_game_file_name_unique(',
        'void saved_game_file_get_useable_untitled_profile_name(',
    ):
        harness += definition(save_source, marker) + '\n'
    harness += r'''
int main(void) {
    const wchar_t profile_name[] = L"Halo player";
    wchar_t untitled[64];
    char path[260];

    assert(saved_game_file_name_unique(profile_name));
    saved_game_file_get_useable_untitled_profile_name(untitled);
    assert(wcscmp(untitled, L"Untitled 1") == 0);
    assert(XCreateSaveGame("u:\\", untitled, CREATE_NEW, 0, path, sizeof(path)) == ERROR_SUCCESS);
    saved_game_file_get_useable_untitled_profile_name(untitled);
    assert(wcscmp(untitled, L"Untitled 2") == 0);
    assert(XCreateSaveGame("u:\\", profile_name, CREATE_NEW, 0, path, sizeof(path)) == ERROR_SUCCESS);
    assert(!saved_game_file_name_unique(profile_name));

    puts("profile-name uniqueness and untitled-name selection passed");
    return 0;
}
'''

    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        source = directory / 'profile_name_test.c'
        source.write_text(harness)
        executable = directory / 'profile_name_test.exe'
        subprocess.run([args.cc, '-std=c11', '-fshort-wchar', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], cwd=directory, check=True)


if __name__ == '__main__':
    main()
