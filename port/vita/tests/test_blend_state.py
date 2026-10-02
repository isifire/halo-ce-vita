"""Execute production blend-state translation; ZERO must never mean unset."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from test_texture_layout import ROOT, definition


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    renderer = (ROOT / 'port/vita/src/d3d8_vitagl.c').read_text()
    pdb = (ROOT / 'port/include/xdk/xdk_pdb.h').read_text()
    harness = '#include <assert.h>\n#include <stdio.h>\ntypedef unsigned int GLenum;\n'
    for name in ['_D3DBLEND', '_D3DBLENDOP', '_D3DRENDERSTATETYPE']:
        harness += re.search(r'enum ' + name + r' \{.*?\};', pdb, re.S).group() + '\n'
    names = ['ZERO', 'ONE', 'SRC_COLOR', 'ONE_MINUS_SRC_COLOR', 'SRC_ALPHA',
             'ONE_MINUS_SRC_ALPHA', 'DST_ALPHA', 'ONE_MINUS_DST_ALPHA',
             'DST_COLOR', 'ONE_MINUS_DST_COLOR', 'SRC_ALPHA_SATURATE',
             'BLEND', 'FUNC_ADD', 'FUNC_SUBTRACT', 'FUNC_REVERSE_SUBTRACT', 'MIN', 'MAX']
    harness += '\n'.join(f'#define GL_{name} {i}' for i, name in enumerate(names))
    harness += '''
static unsigned long D3D__RenderState[D3DRS_MAX];
static GLenum src,dst,equation;
static int enabled;
void glEnable(GLenum cap) { assert(cap==GL_BLEND); enabled=1; }
void glDisable(GLenum cap) { assert(cap==GL_BLEND); enabled=0; }
void glBlendFunc(GLenum a,GLenum b) { src=a; dst=b; }
void glBlendEquation(GLenum value) { equation=value; }
'''
    harness += definition(renderer, 'static GLenum d3d_to_gl_blend(')
    harness += definition(renderer, 'static void vita_apply_blend_state(')
    harness += '''
int main(void) {
 D3D__RenderState[D3DRS_ALPHABLENDENABLE]=1;
 D3D__RenderState[D3DRS_BLENDOP]=D3DBLENDOP_ADD;
 D3D__RenderState[D3DRS_SRCBLEND]=D3DBLEND_ONE;
 D3D__RenderState[D3DRS_DESTBLEND]=D3DBLEND_ZERO;
 vita_apply_blend_state();
 assert(enabled && src==GL_ONE && dst==GL_ZERO && equation==GL_FUNC_ADD);
 D3D__RenderState[D3DRS_SRCBLEND]=D3DBLEND_ZERO;
 D3D__RenderState[D3DRS_DESTBLEND]=D3DBLEND_SRCCOLOR;
 vita_apply_blend_state();
 assert(src==GL_ZERO && dst==GL_SRC_COLOR);
 unsigned long factors[]={D3DBLEND_CONSTANTCOLOR,D3DBLEND_INVCONSTANTCOLOR,D3DBLEND_CONSTANTALPHA,D3DBLEND_INVCONSTANTALPHA};
 GLenum factor_expected[]={GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA};
 for(int i=0;i<4;++i) { D3D__RenderState[D3DRS_SRCBLEND]=factors[i]; vita_apply_blend_state(); assert(src==factor_expected[i]); }
 unsigned long ops[]={D3DBLENDOP_ADD,D3DBLENDOP_SUBTRACT,D3DBLENDOP_REVSUBTRACT,D3DBLENDOP_MIN,D3DBLENDOP_MAX};
 GLenum expected[]={GL_FUNC_ADD,GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT,GL_MIN,GL_MAX};
 for(int i=0;i<5;++i) { D3D__RenderState[D3DRS_BLENDOP]=ops[i]; vita_apply_blend_state(); assert(equation==expected[i]); }
 D3D__RenderState[D3DRS_ALPHABLENDENABLE]=0; vita_apply_blend_state(); assert(!enabled);
 puts("PASS: standard/fallback factors, ZERO, five equations and disabled blending");
}
'''
    with tempfile.TemporaryDirectory(prefix='halo-blend-') as tmp:
        path = Path(tmp)
        (path/'test.c').write_text(harness)
        subprocess.run([args.cc, '-std=gnu11', str(path/'test.c'), '-o', str(path/'test.exe')], check=True)
        subprocess.run([str(path/'test.exe')], check=True)


if __name__ == '__main__':
    main()
