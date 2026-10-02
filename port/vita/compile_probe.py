"""Compile selected real Halo units for ARMv7. This is not a full engine build."""
import argparse
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VARIADIC_PROTOTYPE_FILES = {
    'source/ai/action_uncover.c', 'source/ai/ai.c', 'source/ai/ai_debug.c',
    'source/bungie_net/common/public_key_crypt.c', 'source/camera/editor_flying_camera.c',
    'source/game/cheats.c', 'source/game/game_engine.c', 'source/game/players.c',
    'source/hs/hs.c', 'source/interface/hud_nav_points.c',
    'source/interface/ui_widget_game_data_input_functions.c',
    'source/networking/telnet_console.c', 'source/rasterizer/xbox/rasterizer_xbox_errors.c',
    'source/render/render.c',
}
SOURCES = [
    'source/cache/physical_memory_map.c',
    'port/vita/src/xapi_memory.c', 'port/vita/src/xapi_platform.c',
    'port/vita/tests/platform_contract.c',
    'port/vita/src/xinput_vita.c',
    'source/memory/data.c', 'source/memory/array.c',
    'source/memory/byte_swapping.c', 'source/memory/crc.c',
    'source/memory/circular_queue.c', 'source/memory/data_encoding.c',
    'source/math/real_math.c', 'source/game/game_time.c',
    'source/cache/cache_files.c',
]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--clang', required=True)
    p.add_argument('--out', type=Path, default=ROOT / 'build/vita')
    p.add_argument('--all', action='store_true', help='Compile every non-missing C game unit')
    p.add_argument('--jobs', type=int, default=1)
    p.add_argument('--only-failed', action='store_true', help='Retry failed units from this output directory')
    p.add_argument('--emit-ir', action='store_true', help='Generate LLVM IR for cross-unit ABI inspection')
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    semantics = out / 'halo_msvc_semantics.h'
    import sys
    subprocess.run([sys.executable, 'tools/linux_msvc_semantics.py', '--output', str(semantics),
        '--tags', 'source', '--tags', 'port/include/xdk', '--inlines', 'source',
        '--inlines', 'port/include/xdk'], cwd=ROOT, check=True)
    flags = ['--target=arm-none-eabihf', '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard',
        '-fms-extensions', '-fdeclspec', '-fshort-wchar', '-fsigned-char', '-fcommon',
        '-fno-strict-aliasing', '-fwrapv', '-ffp-contract=off', '-std=gnu89', '-O1',
        '-ffunction-sections', '-fdata-sections',
        '-DHALO_VITA', '-D__STRICT_ANSI__', '-D_XBOX',
        '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion',
        '-Wno-error=incompatible-pointer-types', '-Wno-error=incompatible-function-pointer-types',
        '-Wno-error=implicit-int', '-Wno-error=return-type', '-ferror-limit=8',
        '-include', 'port/vita/include/halo_vita_prefix.h', '-include', str(semantics),
        '-Iport/vita/include', '-Iport/linux/include', '-Iport/linux/src', '-Iport/linux/game', '-Isource', '-Isource/cseries',
        '-isystem', 'port/include/xdk', '-isystem', str(args.sdk.resolve() / 'arm-vita-eabi/include'),
        '-Igpu-hard/arm-vita-eabi/include']
    config = json.loads((ROOT / 'config/config.json').read_text())
    project = next(p for p in config['projects'] if p['name'] == 'halobetacache')
    flags += ['-I' + d for d in project['options']['include_dirs'] if d != 'xbox/include']
    flags += ['-D' + d for d in project['options'].get('defines', [])]
    flags += ['-Wno-ignored-attributes', '-Wno-ignored-pragmas', '-Wno-pragma-pack',
              '-Wno-nonportable-include-path']
    sources = SOURCES
    if args.all:
        sources = [o['name'] for o in project['objects']
                   if o.get('status') != 'Missing' and o['name'].endswith('.c')]
        sources += ['port/vita/src/xapi_memory.c', 'port/vita/src/xapi_platform.c']
        sources += ['port/linux/src/halo_linker_common.c']
        # Supply external definitions for MSVC header inlines. Undefined weak
        # calls otherwise link successfully but return stale ARM registers.
        sources += ['port/linux/game/msvc_comdat.c']
        # Vita executes the Xbox NV2A texture combiners for menu/immediate
        # primitives through VitaGL's ES shader path.
        sources += ['port/linux/src/nv2a_psh.c', 'port/linux/src/nv2a_vsh.c', 'port/linux/src/xgpu_text.c']
        sources += ['port/vita/src/xinput_vita.c']
        sources += ['port/vita/src/arena.c', 'port/vita/src/memory_vita.c']
        sources += ['port/vita/src/bink_null.c', 'port/vita/src/xbdm.c']
        sources += ['port/vita/src/msvc_crt_vita.c', 'port/vita/src/msvc_wide_vita.c']
        sources += ['port/vita/src/d3d8_vitagl.c']
        sources += ['port/vita/src/dsound_vita.c', 'port/vita/src/xnet_vita.c']
        sources += ['port/linux/game/render_interpolation.c']
        sources += ['port/linux/game/network_damage.c', 'port/linux/game/network_distributed.c',
                    'port/linux/game/network_objects.c', 'port/linux/game/network_test.c']
        for p in sorted((ROOT / 'port/third_party/musl-math/src').glob('*.c')):
            sources.append(p.relative_to(ROOT).as_posix())
        absent = [s for s in sources if not (ROOT / s).is_file()]
        (out / 'absent-sources.json').write_text(json.dumps(absent, indent=2))
        sources = [s for s in sources if (ROOT / s).is_file()]
    old = {}
    if args.only_failed:
        old = {r['source']: r for r in json.loads((out / 'compile-results.json').read_text())}
        sources = [s for s in sources if not old.get(s, {}).get('compiled')]
    def compile_one(source):
        name = source.replace('/', '__').replace(' ', '_')[:-2] if args.all else Path(source).stem
        obj = out / (name + ('.ll' if args.emit_ir else '.o'))
        mode = ['-O0', '-S', '-emit-llvm'] if args.emit_ir else ['-c']
        if source.startswith('port/third_party/musl-math'):
            source_flags = [
                '--target=arm-none-eabihf', '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard',
                '-std=gnu11', '-w', '-O2', '-ffunction-sections', '-fdata-sections', '-fshort-wchar',
                '-Iport/third_party/musl-math/include', '-include', 'port/third_party/musl-math/include/libm.h'
            ]
        else:
            source_flags = [*flags]
            if source in VARIADIC_PROTOTYPE_FILES:
                source_flags += ['-include', 'port/vita/include/halo_vita_variadic_prototypes.h']
        result = subprocess.run([args.clang, *source_flags, *mode, source, '-o', str(obj)],
                                cwd=ROOT, capture_output=True, text=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return {'source': source, 'compiled': result.returncode == 0,
                'object': str(obj), 'diagnostics': str(out / (name + '.log'))}
    results = []
    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 8))) as pool:
        for r in pool.map(compile_one, sources):
            results.append(r)
            if not r['compiled'] or not args.all:
                print(('PASS ' if r['compiled'] else 'FAIL ') + r['source'], flush=True)
            elif len(results) % 50 == 0:
                print(f'Compiled {len(results)}/{len(sources)} units', flush=True)
    if args.only_failed:
        old.update({r['source']: r for r in results})
        results = list(old.values())
    (out / 'compile-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    (out / 'objects.rsp').write_text('\n'.join(
        '"' + r['object'].replace('\\', '/') + '"' for r in results if r['compiled']
    ) + '\n', encoding='utf-8')
    print(f"TOTAL {sum(r['compiled'] for r in results)}/{len(results)} compiled", flush=True)
    # Core memory units are required by the runnable proof. Others are a survey.
    if (args.all and not all(r['compiled'] for r in results)) or not all(r['compiled'] for r in results if Path(r['source']).stem in
               ('physical_memory_map', 'xapi_memory', 'xapi_platform', 'crc')):
        raise SystemExit('Required memory units did not compile; inspect their logs.')

if __name__ == '__main__':
    main()
