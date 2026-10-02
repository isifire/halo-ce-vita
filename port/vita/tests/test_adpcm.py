"""Exercise the production Xbox ADPCM decoder with known mono/stereo blocks.

Xbox keeps the header predictor and drops decoded sample 65; see FFmpeg's
ADPCM_IMA_XBOX decoder in libavcodec/adpcm.c. Step 7, nibble 1 yields +2.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--cc', required=True)
    args = p.parse_args()
    source = (ROOT / 'port/vita/src/dsound_vita.c').read_text()
    decoder = source[source.index('static const int ima_index_table'):source.index('static short *decode_pcm')]
    harness = '''
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64
''' + decoder + '''
int main(void) {
    unsigned char blocks[144];
    unsigned long count;
    for (unsigned channels=1; channels<=2; channels++) {
        memset(blocks, 0, sizeof(blocks));
        for (unsigned block=0; block<2; block++) {
            unsigned char *b=blocks+block*36*channels;
            for (unsigned ch=0; ch<channels; ch++) {
                int start=ch ? -1000 : 1000;
                b[4*ch]=start&255; b[4*ch+1]=(start>>8)&255;
                for (unsigned group=0; group<8; group++)
                    memset(b+4*channels+(group*channels+ch)*4, ch?0x99:0x11, 4);
                /* Padding nibble must have no effect on the next block. */
                b[4*channels+(7*channels+ch)*4+3] ^= 0x70;
            }
        }
        short *pcm=decode_adpcm(blocks,72*channels,channels,&count);
        assert(pcm && count==128);
        for (unsigned frame=0; frame<128; frame++)
            for (unsigned ch=0; ch<channels; ch++)
                assert(pcm[frame*channels+ch]==(ch?-1:1)*(1000+2*(frame%64)));
        free(pcm);
    }
    puts("PASS: mono/stereo predictor, 64-sample blocks, rounding, padding and block boundaries");
}
'''
    with tempfile.TemporaryDirectory(prefix='halo-adpcm-') as tmp:
        path = Path(tmp)
        (path / 'test.c').write_text(harness)
        subprocess.run([args.cc, '-std=c99', str(path/'test.c'), '-o', str(path/'test.exe')], check=True)
        subprocess.run([str(path/'test.exe')], check=True)

if __name__ == '__main__':
    main()
