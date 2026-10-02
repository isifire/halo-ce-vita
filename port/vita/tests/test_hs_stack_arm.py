"""Execute production HS stack functions as ARM32, without Vita hardware.

Requires pyelftools and unicorn (optionally installed in build/debug-python).
The unpatched Xbox allocator is a negative control: it MUST fail alignment.
This does not emulate the full game, GXM, or the Vita OS.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'build/debug-python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UcError
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0


def function(source, marker):
    start = source.rindex(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
typedef unsigned char byte;
#define HS_THREAD_STACK_SIZE 512
struct hs_stack_frame {
    struct hs_stack_frame *previous;
    long expression_index;
    void *result;
    short size;
    byte data[2];
};
struct hs_thread_datum {
    short identifier; byte type, flags;
    long script_index, sleep_until, previous_sleep_until;
    struct hs_stack_frame *stack;
    long result;
    byte stack_data[512];
};
typedef char check_frame[sizeof(struct hs_stack_frame)==16 ? 1 : -1];
typedef char check_thread[sizeof(struct hs_thread_datum)==536 ? 1 : -1];
static struct hs_thread_datum thread;
static struct hs_thread_datum *hs_thread_get(long index) { return &thread; }
#define valid_thread(t) ((t)->stack->data+(t)->stack->size <= (t)->stack_data+512)
#define match_hs_assert(file,line,index,condition,message) do { \
    if (!(condition)) __builtin_trap(); \
} while (0)
'''

TEST = r'''
int test_main(void) {
    thread.stack=(struct hs_stack_frame *)thread.stack_data;
    thread.stack->size=0;
    /* Same short/long/real/real sequence as hs_evaluate_arithmetic. */
    for (int depth=0;depth<8;depth++) {
        void *saved[4];
        const long sizes[4]={2,4,4,4};
        for(int i=0;i<4;i++) {
            saved[i]=hs_stack_allocate(0,sizes[i]);
            if ((unsigned long)saved[i]&3) return 1;
            if(i) *(unsigned long *)saved[i]=0x41f00000+i;
        }
        short used=thread.stack->size;
        thread.stack->size=0; /* Resume a suspended evaluator. */
        for(int i=0;i<4;i++) {
            void *p=hs_stack_allocate(0,sizes[i]);
            if(p!=saved[i]) return 2;
            if(i && *(unsigned long *)p!=0x41f00000+i) return 3;
        }
        if(thread.stack->size!=used) return 4;
        struct hs_stack_frame *previous=thread.stack;
        hs_stack_push(0);
        if((unsigned long)thread.stack&3) return 5;
        if(thread.stack->previous!=previous) return 6;
        if((byte *)thread.stack<(byte *)saved[3]+4) return 7;
    }
    /* Odd boolean tails must not misalign the next frame. */
    hs_stack_allocate(0,1);
    hs_stack_push(0);
    if((unsigned long)thread.stack&3) return 8;
    /* Padding is included in the exact end-of-stack check. */
    long remaining=(thread.stack_data+512)-(thread.stack->data+thread.stack->size);
    long padding=(0UL-(unsigned long)(thread.stack->data+thread.stack->size))&3;
    byte *last=hs_stack_allocate(0,remaining-padding);
    if(last+remaining-padding!=thread.stack_data+512) return 9;
#ifdef TEST_OVERFLOW
    hs_stack_allocate(0,1); /* Must trap before writing beyond the stack. */
    return 10;
#endif
    return 0;
}
'''


def run_elf(path, expect_trap=False):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000, 0x100000)
    with path.open('rb') as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD':
                uc.mem_write(seg['p_vaddr'], seg.data())
        entry = elf['e_entry']
    uc.reg_write(UC_ARM_REG_SP, 0x100000)
    uc.reg_write(UC_ARM_REG_LR, 0xf0000)
    try:
        uc.emu_start(entry, 0xf0000, count=100000)
    except UcError as exc:
        if expect_trap and 'Invalid instruction' in str(exc):
            return 0
        raise
    assert not expect_trap, 'overflow was not rejected'
    return uc.reg_read(UC_ARM_REG_R0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--clang', default=r'C:\Program Files\LLVM\bin\clang.exe')
    args = parser.parse_args()
    src = (ROOT/'source/hs/hs_runtime.c').read_text()
    src = HARNESS + function(src, 'static void hs_stack_push(') + function(src, 'static void *hs_stack_allocate(') + TEST
    with tempfile.TemporaryDirectory(prefix='halo-hs-arm-') as tmp:
        tmp = Path(tmp)
        c = tmp/'test.c'
        c.write_text(src)
        for name, defines, expected in [('xbox-negative', [], 1), ('vita', ['-DHALO_VITA'], 0), ('overflow', ['-DHALO_VITA', '-DTEST_OVERFLOW'], 0)]:
            elf = tmp/(name+'.elf')
            subprocess.run([args.clang, '--target=armv7a-none-eabi', '-marm', '-O1', '-nostdlib', '-fuse-ld=lld', '-Wl,-Ttext=0x10000,-e,test_main', *defines, str(c), '-o', str(elf)], check=True)
            result = run_elf(elf, name=='overflow')
            assert result == expected, (name, result, expected)
            print(f'PASS ARM32 {name}: result={result}')


if __name__ == '__main__':
    main()
