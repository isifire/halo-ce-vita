"""Run the actual VitaGL translator/preprocessor on a captured Halo shader pair.

Only GPU types/allocators are stubbed. Outputs are the Cg strings sent to SHARK,
so malformed translation can be reproduced without another console boot.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]

SHIM = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int GLboolean;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_VERTEX_SHADER 0x8b31
#define GL_FRAGMENT_SHADER 0x8b30
#define MAX_CG_TEXCOORD_ID 10
#define MAX_CG_COLOR_ID 2
#define VGL_MODE_SHADER_PAIR 0
#define VGL_MODE_GLOBAL 1
#define VGL_MODE_POSTPONED 2
typedef enum { VGL_TYPE_NONE, VGL_TYPE_TEXCOORD, VGL_TYPE_TEXCOORD_CENTROID,
 VGL_TYPE_COLOR, VGL_TYPE_COLOR_CENTROID, VGL_TYPE_FOG,
 VGL_TYPE_FOG_CENTROID, VGL_TYPE_CLIP } vglSemanticType;
typedef struct {
 char texcoord_names[10][64], color_names[2][64];
 GLboolean texcoord_used[10], color_used[2];
} binds_map;
typedef struct {
 GLenum type; void *prog; char *source; uint32_t size;
 binds_map semantics; GLboolean is_glsl;
} shader;
#define min(a,b) ((a)<(b)?(a):(b))
#define vglMalloc malloc
#define vgl_free free
#define vgl_realloc realloc
#define vgl_fast_memcpy memcpy
#define vgl_memset memset
#define sceClibMemmove memmove
#define vgl_log(...) fprintf(stderr, __VA_ARGS__)
static char *strcasestr(const char *text, const char *needle) {
 size_t n = strlen(needle);
 for (; *text; ++text) if (!_strnicmp(text, needle, n)) return (char *)text;
 return n ? NULL : (char *)text;
}
void vglAddSemanticBinding(const char *s, int idx, vglSemanticType type);
'''

MAIN = r'''
void halo_vita_glsl_checkpoint(const char *message) { puts(message); }
void vglAddSemanticBinding(const char *s, int idx, vglSemanticType type) {
 abort(); /* Shader-pair mode must not need the global bindings API. */
}
static char *read_source(const char *path) {
 FILE *f=fopen(path,"rb"); if (!f) abort();
 fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
 char *s=calloc(1,len+1); if (fread(s,1,len,f)!=(size_t)len) abort();
 fclose(f); return s;
}
int main(int argc,char **argv) {
 if (argc!=5) return 2;
 shader vs={0}, fs={0}; vs.type=GL_VERTEX_SHADER; fs.type=GL_FRAGMENT_SHADER;
 vs.source=read_source(argv[1]); fs.source=read_source(argv[2]);
 glsl_sema_mode=VGL_MODE_SHADER_PAIR;
 glsl_translator_set_process(&vs,&fs);
 FILE *v=fopen(argv[3],"wb"), *f=fopen(argv[4],"wb");
 if (!v || !f) abort();
 fwrite(vs.source,1,strlen(vs.source),v); fclose(v);
 fwrite(fs.source,1,strlen(fs.source),f); fclose(f);
 free(vs.source); free(fs.source);
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--vertex', type=Path, required=True, help='Captured GLSL vertex source from the older build')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/vita-shader-host')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    utils = ROOT / 'gpu-source/vitaGL-master/source/utils'
    vertex = args.vertex.read_text()
    log = args.log.read_text()
    fragment = log.split('vita_combiner: generated fragment bytes=', 1)[1].split('\n', 1)[1]
    fragment = fragment.split('vita_combiner: create vertex shader begin', 1)[0].rstrip()
    (out / 'input.vert').write_text(vertex)
    (out / 'input.frag').write_text(fragment)
    (out / 'host.h').write_text(SHIM)
    (out / 'vitaGL.h').write_text('#include <stdio.h>\n#define vgl_log(...) fprintf(stderr, __VA_ARGS__)\n')
    source = (utils / 'glsl_utils.c').read_text().replace('#include "../shared.h"', '#include "host.h"')
    (out / 'translator.c').write_text(source + MAIN)
    includes = ['-I' + str(out), '-I' + str(utils)]
    obj = out / 'translator.o'
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-g', *includes, '-c', str(out/'translator.c'), '-o', str(obj)], check=True)
    exe = out / 'translate.exe'
    preprocessor = (utils/'preprocessor/preprocessor.cpp').read_text()
    preprocessor = preprocessor.replace('#include <vitasdk.h>', '').replace('#include "../debug_utils.h"', '')
    (out/'preprocessor.cpp').write_text(preprocessor)
    subprocess.run([args.cxx, '-std=gnu++11', '-O0', '-g', *includes, '-I'+str(utils/'preprocessor'), str(obj),
                    str(out/'preprocessor.cpp'), str(utils/'preprocessor/expression.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(out/'input.vert'), str(out/'input.frag'),
                    str(out/'vertex.cg'), str(out/'fragment.cg')], check=True, timeout=30)
    print('Translated shader pair:', out)


if __name__ == '__main__':
    main()
