"""Exercise the production NV2A generators with all 67 shipped vertex programs.

Writes Cg for offline syntax checking (not a replacement for Vita SHARK/GXM).
Optional --cgc accepts the NVIDIA Linux compiler path when running under WSL.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cgc', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/native-shader-tests')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    linux = ROOT / 'port/linux/src'
    pdb = (ROOT / 'port/include/xdk/xdk_pdb.h').read_text()
    enums = '\n'.join(re.search(r'enum ' + name + r' \{.*?\};', pdb, re.S).group()
                      for name in ['_D3DCMPFUNC', '_D3DFOGMODE', '_D3DRENDERSTATETYPE'])
    header = (linux / 'xgpu.h').read_text()
    header = header[header.index('struct xgpu_text'):header.index('/* ---------- textures */')]
    (out / 'xgpu.h').write_text('#pragma once\n#include <stdint.h>\n'
        'typedef uint32_t DWORD; typedef int BOOL;\n#define TRUE 1\n#define FALSE 0\n' + enums + header)
    for name in ['nv2a_vsh.c', 'nv2a_psh.c', 'xgpu_text.c', 'nv2a_cg.h', 'port_config.h']:
        (out / name).write_text((linux / name).read_text())
    data_dir = ROOT / 'source/rasterizer/xbox'
    entries = re.findall(r'VERTEX_SHADER_ENTRY\((0x[0-9A-Fa-f]+),',
                         (data_dir / 'rasterizer_xbox_vertex_shaders.c').read_text())
    offsets = ','.join(entries)
    data = (data_dir / 'rasterizer_xbox_vertex_shaders_data.inc').read_text()
    harness = r'''
#include "xgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
int config_boolean(const char *name) { return 0; }
const char *config_string(const char *name) { return ""; }
static const DWORD code[]={ DATA };
static const unsigned offsets[]={ OFFSETS };
static void save(const char *name, char *text) {
    assert(text && strstr(text,"void main()"));
    assert(!strstr(text,"#version") && !strstr(text,"layout("));
    FILE *f=fopen(name,"wb"); assert(f);
    fwrite(text,1,strlen(text),f); fclose(f); free(text);
}
int main(void) {
    char name[64];
    for (unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i) {
        const DWORD *p=code+offsets[i]/4;
        assert((p[0]>>16)>0 && (p[0]>>16)<=136);
        snprintf(name,sizeof(name),"vertex_%02u.cg",i);
        save(name,nv2a_vertex_shader_to_glsl(p+1,p[0]>>16,0));
    }
    /* Four stages, all hardware texture modes, fog and alpha testing. */
    for (unsigned mode=0;mode<=18;++mode) {
        struct nv2a_pixel_shader_key key={0};
        key.texture_modes=1|(mode<<5)|(mode<<10)|(mode<<15);
        memset(key.sampler_type,_xgpu_sampler_2d,4);
        key.combiner_state[D3DRS_PSCOMBINERCOUNT]=8;
        for(unsigned i=0;i<8;++i) {
            key.combiner_state[D3DRS_PSRGBINPUTS0+i]=0x08040905;
            key.combiner_state[D3DRS_PSALPHAINPUTS0+i]=0x18141915;
            key.combiner_state[D3DRS_PSRGBOUTPUTS0+i]=0x00000c00;
            key.combiner_state[D3DRS_PSALPHAOUTPUTS0+i]=0x00000c00;
        }
        key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSABCD]=0x0000000c;
        key.combiner_state[D3DRS_PSFINALCOMBINERINPUTSEFG]=0x00001c00;
        key.alpha_test_function=D3DCMP_GREATER;
        key.fog_enable=1; key.fog_table_mode=D3DFOG_EXP2;
        snprintf(name,sizeof(name),"fragment_%02u.cg",mode);
        save(name,nv2a_pixel_shader_to_glsl(&key));
    }
}
'''.replace(' DATA ', data).replace(' OFFSETS ', offsets)
    (out / 'main.c').write_text(harness)
    exe = out / 'generate.exe'
    subprocess.run([args.cc, '-DHALO_VITA', '-std=gnu11', '-I'+str(out),
                    *[str(out / s) for s in ['main.c', 'nv2a_vsh.c', 'nv2a_psh.c', 'xgpu_text.c']],
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=out, check=True)
    files = sorted(out.glob('vertex_*.cg')) + sorted(out.glob('fragment_*.cg'))
    assert len(files) == len(entries) + 19
    # Device regression: SHARK rejects clamp(int,int,int), even though NVIDIA
    # gp4vp accepts it. These two original programs failed in revision 60 logs.
    for index in [9, 47]:
        generated = (out / f'vertex_{index:02d}.cg').read_text()
        assert 'c[nv2a_constant_index(a0 +' in generated
        assert 'clamp(a0' not in generated
    # Execute the emitted integer helper itself against boundary cases.
    helper = re.search(r'int nv2a_constant_index\(int value\) \{[^}]+\}', generated).group()
    (out / 'index_test.c').write_text('#include <assert.h>\n#include <limits.h>\n' + helper + '''
int main(void) {
 assert(nv2a_constant_index(INT_MIN)==0);
 assert(nv2a_constant_index(-1)==0);
 assert(nv2a_constant_index(0)==0);
 for(int i=1;i<192;++i) assert(nv2a_constant_index(i)==i);
 assert(nv2a_constant_index(192)==191);
 assert(nv2a_constant_index(INT_MAX)==191);
}
''')
    subprocess.run([args.cc, str(out/'index_test.c'), '-o', str(out/'index_test.exe')], check=True)
    subprocess.run([str(out/'index_test.exe')], check=True)
    if args.cgc:
        for file in files:
            profile = 'gp4vp' if file.name.startswith('vertex') else 'gp4fp'
            result = subprocess.run([str(args.cgc.resolve()), '-profile', profile, str(file),
                                     '-o', str(file.with_suffix('.asm'))], capture_output=True, text=True)
            file.with_suffix('.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(file.name + ':\n' + result.stdout + result.stderr)
    print(f'PASS: {len(entries)} original vertex programs + 19 fragment-mode cases' +
          (' compiled with Cg' if args.cgc else ' generated; GPU compilation not tested'))


if __name__ == '__main__':
    main()
