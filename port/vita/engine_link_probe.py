"""Perform real partial and SDK-assisted links and package the Vita executable.

Keeps all game sections to expose every dependency. This is broader than the
reachable startup graph; successful linking alone does not imply runtime
compatibility, so hardware boot logs remain part of validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    records = json.loads(a.results.read_text())
    if not records or any(not r['compiled'] for r in records):
        raise SystemExit('Incomplete compilation; refusing a partial inventory')
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    sdk = a.sdk.resolve()
    def tool(name):
        base = sdk / 'bin' / ('arm-vita-eabi-' + name)
        return str(base.with_suffix('.exe') if base.with_suffix('.exe').exists() else base)
    rsp = out / 'objects.rsp'
    root=Path(__file__).resolve().parents[2]
    records = [r for r in records if not r['source'].endswith(('arena.c', 'memory_vita.c'))]
    for name in ('arena','memory_vita'):
        source=root/'port/vita/src'/ (name+'.c')
        obj=out/(name+'.o')
        cmd=[tool('gcc'),'-O2','-std=gnu11','-I'+str(root/'port/vita/include'),
             '-c',str(source),'-o',str(obj)]
        native=subprocess.run(cmd,capture_output=True,text=True)
        (out/(name+'-compile.log')).write_text(native.stdout+native.stderr)
        if native.returncode: raise SystemExit(native.stderr)
        records.append({'source':'port/vita/src/'+name+'.c','object':str(obj),'compiled':True})
    rsp.write_text('\n'.join('"'+Path(r['object']).resolve().as_posix()+'"' for r in records)+'\n')
    combined = out / 'engine-combined.o'
    partial_cmd = [tool('ld'), '-r', '-o', str(combined), '@'+str(rsp)]
    partial = subprocess.run(partial_cmd, capture_output=True, text=True)
    (out/'partial-link.log').write_text(partial.stdout+partial.stderr)
    summary = {'objects':len(records), 'partial_link_exit':partial.returncode,
               'partial_command':partial_cmd, 'runtime_validated':False}
    if partial.returncode == 0:
        # Real archive extraction, including dependencies introduced by archives.
        # No unresolved-symbol suppression and no garbage collection.
        cmd = [tool('gcc'), '-nostartfiles', '-Wl,-e,main_loop',
               '-Wl,--no-wchar-size-warning', '-Wl,--no-enum-size-warning',
               '-Wl,-Map,'+(out/'engine.map').as_posix(), str(combined),
               '-L'+str(root/'gpu-hard/arm-vita-eabi/lib'),
               '-Wl,--start-group', '-lvitaGL', '-lvitashark', '-lmathneon', '-lstdc++', '-lc', '-lm', '-lgcc',
               '-lSceLibKernel_stub', '-lSceIofilemgr_stub', '-lSceCtrl_stub', '-lSceRtc_stub',
               '-lSceGxm_stub', '-lSceDisplay_stub', '-lSceShaccCg_stub', '-lSceAppMgr_stub', '-lSceCommonDialog_stub',
               '-lSceAudio_stub', '-lSceNet_stub', '-lSceNetCtl_stub', '-lSceSysmodule_stub',
               '-Wl,--end-group', '-o', str(out/'engine-link-probe.elf')]
        result = subprocess.run(cmd,capture_output=True,text=True)
        log = result.stdout+result.stderr
        (out/'sdk-link.log').write_text(log)
        unresolved=sorted(set(re.findall(r"undefined reference to [`']([^'`]+)['`]",log)))
        summary.update(sdk_link_exit=result.returncode, sdk_command=cmd,
                       undefined_unique=len(unresolved), undefined=unresolved,
                       duplicate_definitions=sorted(set(re.findall(r"multiple definition of [`']([^'`]+)['`]",log))))
        if result.returncode == 0:
            elf_reloc = out / 'halo-vita-full.elf'
            velf = out / 'halo-vita-full.velf'
            eboot = out / 'eboot.bin'
            sfo = out / 'param.sfo'
            vpk = out / 'halo-ce-vita.vpk'
            full_cmd = [tool('gcc'), '-Wl,-q', '-Wl,--no-wchar-size-warning', '-Wl,--no-enum-size-warning',
                        '-Wl,-Map,' + (out/'engine-full.map').as_posix(), str(combined),
                        '-L' + str(root/'gpu-hard/arm-vita-eabi/lib'),
                        '-Wl,--start-group', '-lvitaGL', '-lvitashark', '-lmathneon', '-lstdc++', '-lc', '-lm', '-lgcc',
                        '-lSceLibKernel_stub', '-lSceIofilemgr_stub', '-lSceCtrl_stub', '-lSceRtc_stub',
                        '-lSceGxm_stub', '-lSceDisplay_stub', '-lSceShaccCg_stub', '-lSceAppMgr_stub', '-lSceCommonDialog_stub',
                        '-lSceAudio_stub', '-lSceNet_stub', '-lSceNetCtl_stub', '-lSceSysmodule_stub',
                        '-Wl,--end-group', '-o', str(elf_reloc)]
            subprocess.run(full_cmd, capture_output=True, check=True)
            vita_elf_create = sdk / 'bin' / ('vita-elf-create' + ('.exe' if (sdk/'bin/vita-elf-create.exe').exists() else ''))
            subprocess.run([str(vita_elf_create), str(elf_reloc), str(velf)], capture_output=True, check=True)
            vita_make_fself = sdk / 'bin' / ('vita-make-fself' + ('.exe' if (sdk/'bin/vita-make-fself.exe').exists() else ''))
            subprocess.run([str(vita_make_fself), '-c', str(velf), str(eboot)], capture_output=True, check=True)
            vita_mksfoex = sdk / 'bin' / ('vita-mksfoex' + ('.exe' if (sdk/'bin/vita-mksfoex.exe').exists() else ''))
            subprocess.run([str(vita_mksfoex), '-s', 'TITLE_ID=HALOCE001', 'Halo Combat Evolved', str(sfo)], capture_output=True, check=True)
            vita_pack_vpk = sdk / 'bin' / ('vita-pack-vpk' + ('.exe' if (sdk/'bin/vita-pack-vpk.exe').exists() else ''))
            pack_cmd = [
                str(vita_pack_vpk), '-s', str(sfo), '-b', str(eboot),
                '-a', str(root / 'port/vita/sce_sys/icon0.png') + '=sce_sys/icon0.png',
                '-a', str(root / 'port/vita/sce_sys/livearea/contents/bg.png') + '=sce_sys/livearea/contents/bg.png',
                '-a', str(root / 'port/vita/sce_sys/livearea/contents/startup.png') + '=sce_sys/livearea/contents/startup.png',
                '-a', str(root / 'port/vita/sce_sys/livearea/contents/template.xml') + '=sce_sys/livearea/contents/template.xml',
                str(vpk)
            ]
            subprocess.run(pack_cmd, capture_output=True, check=True)
            summary['vpk'] = str(vpk)
            print('VPK PACKAGED: ' + str(vpk))
    summary['objects_sha256'] = {r['source']:hashlib.sha256(Path(r['object']).read_bytes()).hexdigest() for r in records}
    (out/'link-probe.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('PARTIAL_LINK exit='+str(partial.returncode))
    if partial.returncode: print(partial.stderr[:4000])
    else: print('SDK_LINK exit=%s unresolved=%s' % (summary['sdk_link_exit'],summary['undefined_unique']))
    print(out/'link-probe.json')
    if partial.returncode: raise SystemExit(partial.returncode)


if __name__=='__main__': main()
