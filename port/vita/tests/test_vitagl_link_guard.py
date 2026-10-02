"""Execute the production link preflight with simulated Cg compiler failure.

GXM reflection is replaced with a counter: failed compilation must never reach it.
Also exercises real indexed-uniform lookup, including program 0/uniform 0.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_texture_layout import ROOT, definition


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    source = (ROOT / 'gpu-source/vitaGL-master/source/custom_shaders.c').read_text()
    link = source[source.index('void glLinkProgram(GLuint progr)'):]
    link = link[:link.index('\n\tif (p->status == PROG_LINKED)')] + '\n++reflections;\n}\n'
    union = source[source.index('typedef union {', source.index('#ifdef STRICT_UNIFORMS_COMPLIANCE')):]
    union = union[:union.index('} uniform_location;') + len('} uniform_location;')]
    lookup = definition(source, 'GLint glGetUniformLocation(')
    decode = definition(source, 'static inline __attribute__((always_inline)) uniform *get_uniform_from_ptr(')
    harness = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define STRICT_UNIFORMS_COMPLIANCE
#define THREAD_SAFE()
#define MAX_CUSTOM_PROGRAMS 2
#define GL_TRUE 1
#define GL_FALSE 0
#define VGL_MODE_POSTPONED 2
#define VGL_MODE_SHADER_PAIR 0
#define vgl_log(...) ((void)0)
#define vgl_free free
typedef unsigned GLuint;
typedef int GLint;
typedef char GLchar;
typedef struct {int array_size;} SceGxmProgramParameter;
typedef struct { void *prog; int is_glsl; char *source; int success; } shader;
typedef struct { const SceGxmProgramParameter *ptr; } uniform;
typedef struct { int idx; char *name; } attrib;
typedef struct {shader *vshader,*fshader; attrib *glsl_attr_map; int num_glsl_attr;
 uniform *vert_uniforms,*frag_uniforms; unsigned vert_uniforms_num,frag_uniforms_num;} program;
static program progs[2];
static int glsl_sema_mode=VGL_MODE_POSTPONED, reflections;
static SceGxmProgramParameter param={4};
static void halo_vita_glsl_checkpoint(const char *s) {}
static void glsl_translator_set_process(shader *a,shader *b) {}
static void vgl_compile_shader(shader *s,int save) {s->prog=s->success?s:NULL;}
static void glBindAttribLocation(unsigned p,int i,const char *n) {}
#define set_default_attrib_binding() (++reflections)
static const SceGxmProgramParameter *sceGxmProgramFindParameterByName(void *p,const char *name) {
 return p && !strcmp(name,"values") ? &param : NULL;
}
static int sceGxmProgramParameterGetArraySize(const SceGxmProgramParameter *p) {return p->array_size;}
'''
    harness += union + decode + lookup + link + r'''
int main(void) {
 shader v={NULL,1,"vertex",0},f={NULL,1,"fragment",0};
 progs[0].vshader=&v; progs[0].fshader=&f;
 for(int mask=0;mask<4;mask++) {
   v.prog=f.prog=NULL; v.success=mask&1; f.success=mask&2;
   glsl_sema_mode=VGL_MODE_POSTPONED; reflections=0;
   glLinkProgram(1);
   assert(reflections==(mask==3?2:0));
   assert(glsl_sema_mode==VGL_MODE_POSTPONED);
 }
 reflections=0; glLinkProgram(0); glLinkProgram(3); glLinkProgram(2);
 assert(reflections==0);
 uniform u={&param};
 v.prog=NULL; f.prog=&f;
 progs[0].frag_uniforms=&u; progs[0].frag_uniforms_num=1;
 int location=glGetUniformLocation(1,"values[0]");
 assert(location==1); /* zero must not alias the no-op sentinel */
 uint32_t offset=99;
 assert(get_uniform_from_ptr(location,&offset)==&u && offset==0);
 location=glGetUniformLocation(1,"values[3]");
 assert(get_uniform_from_ptr(location,&offset)==&u && offset==3);
 assert(glGetUniformLocation(1,"values[4]")==-1);
 assert(glGetUniformLocation(1,"values[-1]")==-1);
 puts("PASS: rejected shader pairs never enter GXM; uniform array locations and bounds");
}
'''
    with tempfile.TemporaryDirectory(prefix='halo-link-test-') as tmp:
        path = Path(tmp)
        (path / 'test.c').write_text(harness)
        exe = path / 'test.exe'
        subprocess.run([args.cc, '-std=gnu11', str(path/'test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
