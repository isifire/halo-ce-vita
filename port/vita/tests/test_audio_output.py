"""Check that the production audio thread preserves a queued PCM buffer."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_texture_layout import definition, ROOT

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    source = (ROOT/'port/vita/src/dsound_vita.c').read_text()
    metrics = source[source.index('struct audio_diag_window'):source.index('static struct audio_diag_window')]
    harness = '''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MIX_CHUNK_FRAMES 1024
#define OUTPUT_CHANNELS 2
#define OUTPUT_RATE 48000
typedef unsigned SceSize;
static int audio_running=1, audio_port=7, submissions=0;
static const short *queued;
static short snapshot[MIX_CHUNK_FRAMES*2];
static void mixer_lock(void) {}
static void mixer_unlock(void) {}
static void sceKernelDelayThread(unsigned us) {}
static void mix(float *out,unsigned long n,unsigned long *voices,unsigned long *packets) {
    *voices=1; *packets=4;
    for(unsigned long i=0;i<n*2;i++) out[i]=(submissions+1)*0.1f;
}
static int sceAudioOutOutput(int port,const void *buf) {
    if(queued) {
        assert(queued!=buf);
        assert(!memcmp(queued,snapshot,sizeof(snapshot)));
    }
    queued=buf;
    memcpy(snapshot,buf,sizeof(snapshot));
    if(++submissions==5) audio_running=0;
    return 1024;
}
''' + metrics + '\nstatic struct audio_diag_window audio_diag;\n'
    harness += definition(source,'static int audio_thread_func(')
    harness += '''
#define TRUE 1
struct vita_stream { struct vita_stream *next; int paused; unsigned packet_count, sample_rate; };
static struct vita_stream voice={0,0,4,22050}, *streams=&voice;
static int game_audio_ready=0, mix_calls=0;
static void mix_voice(struct vita_stream *s,float *out,unsigned long frames) { ++mix_calls; }
'''
    harness += definition(source,'void halo_vita_audio_frame_presented(')
    harness += definition(source,'static void mix(').replace('static void mix(', 'static void test_startup_mix(', 1)
    harness += '''
int main(void) {
    float out[8]={1,1,1,1,1,1,1,1}; unsigned long voices,packets;
    test_startup_mix(out,4,&voices,&packets);
    assert(mix_calls==0 && voices==0 && packets==0);
    for(int i=0;i<8;i++) assert(out[i]==0);
    halo_vita_audio_frame_presented();
    test_startup_mix(out,4,&voices,&packets);
    assert(mix_calls==1 && voices==1 && packets==4);
    audio_thread_func(0,0); assert(submissions==5);
    puts("PASS: startup gate preserves queued audio; queued PCM remains unchanged across five submissions");
}
'''
    with tempfile.TemporaryDirectory(prefix='halo-audio-output-') as tmp:
        path=Path(tmp)
        (path/'test.c').write_text(harness)
        subprocess.run([args.cc,'-std=c99',str(path/'test.c'),'-o',str(path/'test.exe'),'-lm'],check=True)
        subprocess.run([str(path/'test.exe')],check=True)

if __name__=='__main__':
    main()
