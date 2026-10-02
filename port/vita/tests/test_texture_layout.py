"""Run the actual CPU texture allocation/lock functions with minimal host types.

No GL calls are substituted: the selected functions do not use GL. This checks
the production mip offsets without requiring a Vita GPU.
"""
from pathlib import Path
import subprocess
import tempfile
import argparse

ROOT = Path(__file__).resolve().parents[3]


def definition(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    source = (ROOT / 'port/vita/src/d3d8_vitagl.c').read_text()
    platform = (ROOT / 'port/vita/src/xapi_platform.c').read_text()
    headers = (ROOT / 'port/include/xdk/xdk_pdb.h').read_text()
    parts = ['''#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define WINAPI
#define S_OK 0
#define E_INVALIDARG -1
#define E_OUTOFMEMORY -2
#define D3DCOMMON_TYPE_TEXTURE 0x40000
#define D3DCOMMON_D3DCREATED 0x1000000
#define D3DFORMAT_FORMAT_SHIFT 8
#define D3DFORMAT_DIMENSION_SHIFT 4
typedef uintptr_t DWORD;
typedef const char *LPCSTR;
typedef const unsigned short *LPCWSTR;
struct D3DCubeTexture { DWORD Common, Data, Lock, Format, Size; };
struct D3DVolumeTexture { DWORD Common, Data, Lock, Format, Size; };
struct _D3DLOCKED_RECT { int Pitch; void *pBits; };
struct _D3DLOCKED_BOX { int RowPitch, SlicePitch; void *pBits; };
struct tagRECT; struct _D3DBOX;
enum _D3DCUBEMAP_FACES { FACE_0, FACE_1, FACE_2, FACE_3, FACE_4, FACE_5 };
''', definition(headers, 'enum _D3DFORMAT {') + ';']
    for marker in [
        'static unsigned int vita_format_bytes_per_pixel(',
        'static int vita_is_dxt_format(',
        'static unsigned int vita_mip_bytes(',
        'static unsigned int vita_mip_dimension(',
        'struct d3d_cube_texture_impl {',
        'struct d3d_volume_texture_impl {',
        'long WINAPI D3DDevice_CreateCubeTexture(',
        'void WINAPI D3DCubeTexture_LockRect(',
        'long WINAPI D3DDevice_CreateVolumeTexture(',
        'void WINAPI D3DVolumeTexture_LockBox(',
    ]:
        parts.append(definition(source, marker) + (';' if marker.startswith('struct ') else ''))
    parts.append(definition(platform, 'static int save_path('))
    parts.append('''
int main(void) {
    char save_a[260], save_b[260]; unsigned short saved[128];
    const unsigned short name_a[] = {65, 0}, name_b[] = {66, 0};
    assert(save_path("u:", name_a, save_a, sizeof(save_a), saved));
    assert(saved[0] == 65 && saved[1] == 0);
    assert(save_path("u:", name_b, save_b, sizeof(save_b), saved));
    assert(strcmp(save_a, save_b));
    assert(!save_path("u:", name_a, save_a, 2, saved));
    assert(!save_path(NULL, name_a, save_a, sizeof(save_a), saved));
    struct D3DCubeTexture *cube;
    struct _D3DLOCKED_RECT rect;
    assert(D3DDevice_CreateCubeTexture(8, 0, 0, D3DFMT_DXT1, 0, &cube) == S_OK);
    struct d3d_cube_texture_impl *c = (void *)cube;
    assert(c->levels == 4 && c->face_size == 56);
    assert(c->offsets[0] == 0 && c->offsets[1] == 32 && c->offsets[2] == 40 && c->offsets[3] == 48);
    unsigned int face, level;
    for (face = 0; face < 6; ++face) for (level = 0; level < 4; ++level) {
        D3DCubeTexture_LockRect(cube, face, level, &rect, NULL, 0);
        assert(rect.pBits == c->pixels + face * 56 + c->offsets[level]);
        unsigned int side = vita_mip_dimension(8, level);
        memset(rect.pBits, face * 4 + level + 1, vita_mip_bytes(D3DFMT_DXT1, side, side, 1));
    }
    for (face = 0; face < 6; ++face) for (level = 0; level < 4; ++level)
        assert(c->pixels[face * 56 + c->offsets[level]] == face * 4 + level + 1);
    D3DCubeTexture_LockRect(cube, 6, 0, &rect, NULL, 0); assert(!rect.pBits);
    D3DCubeTexture_LockRect(cube, 0, 4, &rect, NULL, 0); assert(!rect.pBits);
    free(cube);
    struct D3DVolumeTexture *volume;
    struct _D3DLOCKED_BOX box;
    assert(D3DDevice_CreateVolumeTexture(8, 4, 2, 0, 0, D3DFMT_A8, 0, &volume) == S_OK);
    struct d3d_volume_texture_impl *v = (void *)volume;
    assert(v->levels == 4 && v->offsets[1] == 64 && v->offsets[2] == 72 && v->offsets[3] == 74);
    D3DVolumeTexture_LockBox(volume, 1, &box, NULL, 0);
    assert(box.RowPitch == 4 && box.SlicePitch == 8 && box.pBits == v->pixels + 64);
    D3DVolumeTexture_LockBox(volume, 4, &box, NULL, 0); assert(!box.pBits);
    free(volume);
    puts("PASS: mip tails, six faces, volume offsets, invalid levels, distinct save names and path bounds");
    return 0;
}
''')
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / 'test.c').write_text('\n'.join(parts))
        subprocess.run([args.cc, '-std=c11', str(path / 'test.c'), '-o', str(path / 'test.exe')], check=True)
        subprocess.run([str(path / 'test.exe')], check=True)


if __name__ == '__main__':
    main()
