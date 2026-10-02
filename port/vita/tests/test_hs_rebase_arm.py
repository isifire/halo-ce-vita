"""Run the complete production Vita tag rebase on captured tags as ARM32.

The negative control protects only syntax, as revision 79 did. It must
corrupt at least one valid script/global handle in the supplied a10 image.
No game assets are bundled, and this is not full-system Vita emulation.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'build/debug-python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC

BASE = 0x803a6000
DEST = 0x842a6000
WRAPPER = r'''
void *physical_memory_get_game_state_base_address(void) { return (void *)0x83f00000UL; }
FILE *fopen(const char *path, const char *mode) { return NULL; }
int fprintf(FILE *f, const char *fmt, ...) { return 0; }
int fclose(FILE *f) { return 0; }
unsigned long test_layout[] = {
    offsetof(struct scenario, hs_syntax_data),
    offsetof(struct scenario, hs_string_constants),
    offsetof(struct scenario, hs_scripts),
    offsetof(struct scenario, hs_globals),
    offsetof(struct tag_data, address),
    sizeof(struct hs_script), offsetof(struct hs_script, root_expression_index),
    sizeof(struct hs_global), offsetof(struct hs_global, initialization_expression_index)
};
int test_rebase(void *p, unsigned long bytes) {
    /* Keep the layout symbol alive under section GC. */
    __asm__ volatile ("" : : "r"(test_layout));
    return cache_file_rebase_vita_pointers(p, bytes);
}
'''


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def execute(elf_path, tags):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.mem_map(0x10000, 0x200000)
    uc.mem_map(DEST & ~4095, (len(tags)+4095)&~4095)
    uc.mem_write(DEST, tags)
    with elf_path.open('rb') as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type']=='PT_LOAD':
                uc.mem_write(seg['p_vaddr'], seg.data())
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        layout = struct.unpack('<9I', uc.mem_read(symbols['test_layout'], 36))
        entry = symbols['test_rebase']
    uc.reg_write(UC_ARM_REG_SP, 0x200000)
    uc.reg_write(UC_ARM_REG_LR, 0x1f0000)
    uc.reg_write(UC_ARM_REG_R0, DEST)
    uc.reg_write(UC_ARM_REG_R1, len(tags))
    uc.emu_start(entry, 0x1f0000, timeout=120000000)
    assert uc.reg_read(UC_ARM_REG_PC)==0x1f0000, 'emulation timed out'
    assert uc.reg_read(UC_ARM_REG_R0)==1, 'rebase rejected valid capture'
    return bytes(uc.mem_read(DEST,len(tags))), layout


def check(original, result, layout, negative):
    syntax_field, strings_field, scripts_field, globals_field, data_address, script_size, script_root, global_size, global_root = layout
    table = u32(original,0)-BASE
    scenario_index = u32(original,4)&0xffff
    scenario = u32(original,table+32*scenario_index+20)-BASE
    syntax = u32(original,scenario+syntax_field+data_address)-BASE
    syntax_bytes = u32(original,scenario+syntax_field)
    node_count = struct.unpack_from('<h', original, syntax+46)[0]
    roots = 0
    corrupted = []
    for field,size,root_offset in [(scripts_field,script_size,script_root),(globals_field,global_size,global_root)]:
        count = u32(original,scenario+field)
        if not count:
            assert u32(result,scenario+field+4)==u32(original,scenario+field+4)
            continue
        start = u32(original,scenario+field+4)-BASE
        assert u32(result,scenario+field+4)==DEST+start, 'block pointer not relocated'
        for i in range(count):
            pos = start+i*size
            handle = u32(original,pos+root_offset)
            if handle==0xffffffff:
                continue
            idx = handle&0xffff
            assert idx<node_count
            identifier = struct.unpack_from('<H',original,syntax+56+20*idx)[0]
            assert identifier==handle>>16, 'original root is already invalid'
            roots += 1
            changed = u32(result,pos+root_offset)
            if changed!=handle:
                name=original[pos:pos+32].split(b'\0')[0].decode('ascii','replace')
                corrupted.append((name,hex(handle),hex(changed)))
        if not negative:
            assert result[start:start+count*size]==original[start:start+count*size]
    assert result[syntax:syntax+syntax_bytes]==original[syntax:syntax+syntax_bytes]
    if negative:
        assert ('bridge_flavor_cycle','0x818f1e1c','0x857f1e1c') in corrupted
        print('PASS negative control reproduces logged failure:',corrupted,flush=True)
    else:
        assert not corrupted
        start = u32(original,scenario+strings_field+data_address)-BASE
        size = u32(original,scenario+strings_field)
        assert result[start:start+size]==original[start:start+size]
        print(f'PASS ARM full rebase: {roots} roots valid, {node_count} nodes unchanged, strings unchanged, block pointers relocated',flush=True)


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tags',type=Path,required=True)
    p.add_argument('--negative-control',action='store_true',help='Require reproduction of the a10 revision-79 failure')
    p.add_argument('--clang',default=r'C:\Program Files\LLVM\bin\clang.exe')
    a=p.parse_args()
    tags=a.tags.read_bytes()
    source=(ROOT/'source/cache/cache_files.c').read_text()
    project=next(p for p in json.loads((ROOT/'config/config.json').read_text())['projects'] if p['name']=='halobetacache')
    with tempfile.TemporaryDirectory(prefix='halo-rebase-arm-') as tmp:
        tmp=Path(tmp)
        semantics=tmp/'semantics.h'
        subprocess.run([sys.executable,'tools/linux_msvc_semantics.py','--output',str(semantics),'--tags','source','--tags','port/include/xdk','--inlines','source','--inlines','port/include/xdk'],cwd=ROOT,check=True,capture_output=True)
        flags=['--target=arm-none-eabihf','-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=hard','-fms-extensions','-fdeclspec','-fshort-wchar','-fsigned-char','-fcommon','-fno-strict-aliasing','-fwrapv','-std=gnu89','-O1','-w','-ffunction-sections','-fdata-sections','-DHALO_VITA','-D__STRICT_ANSI__','-D_XBOX','-include','port/vita/include/halo_vita_prefix.h','-include',str(semantics)]
        flags += ['-I'+d for d in ['port/vita/include','port/linux/include','port/linux/src','port/linux/game','source','source/cseries','source/cache','port/include/xdk','sdk-extracted/vitasdk/arm-vita-eabi/include','gpu-hard/arm-vita-eabi/include']]
        flags += ['-I'+d for d in project['options']['include_dirs'] if d!='xbox/include']
        flags += ['-D'+d for d in project['options'].get('defines',[])]
        for negative in ([True,False] if a.negative_control else [False]):
            test_source=source.replace('\t\texclusions,\n\t\t4,','\t\texclusions,\n\t\t1,') if negative else source
            c=tmp/'test.c'; c.write_text(test_source+WRAPPER)
            elf=tmp/'test.elf'
            subprocess.run([a.clang,*flags,str(c),'-nostdlib','-fuse-ld=lld','-Wl,--gc-sections,-Ttext=0x10000,-e,test_rebase','-o',str(elf)],cwd=ROOT,check=True)
            result,layout=execute(elf,tags)
            check(tags,result,layout,negative)


if __name__=='__main__':
    main()
