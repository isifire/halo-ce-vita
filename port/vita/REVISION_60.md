# Revision 60: crash and rendering audit

## Confirmed crash

Input: `psp2core-1790808840-0x00134c2323-eboot.bin.psp2dmp` and the supplied
Desktop `boot.log`, matched to the revision 59 ELF. Both deferred shader
compilations fail. VitaGL nevertheless calls GXM reflection on a null program.
The dump reports DFAR `0x24`, PC in SceGxm, and LR `0x81266fa9`.
Subtracting the application relocation `0x31000` gives `0x81235fa8`, which
symbolizes to `glLinkProgram`, at the default attribute-binding reflection.

The library now rejects incomplete/failed shader pairs before reflection,
records actual SHARK diagnostics, and checks binary allocation/registration.
Failed shader logs are freed even without a compiled program.

## Rendering corrections

- Preserve and execute Xbox vertex microcode through the shared Linux NV2A
  translator. Previously Vita retained only its instruction count and used a
  generic matrix transform, duplicating one UV into all four texture stages.
- Implement separate loaded-program slots and vertex declarations, all 192
  constants, viewport constants, all 16 input registers, packed normals, BGRA
  vertex colors and FLOAT2H inputs. Keep the working text-atlas path unchanged.
- Emit native Cg with explicit semantics from the shared translators and create
  shaders with the Cg-specific VitaGL enums. No general GLSL rewriting on this path.
- Include vertex-program identity in the material cache and cache rejected
  pairs as well, avoiding repeated compilation every frame.
- Enable indexed-uniform support in VitaGL. Fix the zero-location collision
  and reject array indices outside the declared size. Upload sampler selectors,
  all original constants, texture scale and bump-environment parameters.

The earlier suspicion that signed locations alone caused the missing uniforms
was not established: default VitaGL negates pointers before returning locations.
The verified array issue is that the previous build did not enable its indexed
uniform lookup while the renderer requested names such as `ps_c0[0]`.

## Validation performed

- 513/513 engine translation units compiled; final link has zero unresolved symbols.
- `test_native_shaders.py`: all 67 original Xbox vertex programs and 19 generated
  fragment-mode cases compile with NVIDIA Cg 3.1 (`gp4vp`/`gp4fp`). This checks the
  generated language, NOT Vita SHARK profiles or actual rendered output.
- The old translated pair fails NVIDIA Cg parsing in its compatibility prelude.
  Its exact SHARK rejection reason was absent from the old build's logs.
- `test_vitagl_link_guard.py`: all four success/failure combinations, invalid
  handles, indexed-uniform lookup and bounds pass against production source.
- Texture-layout, save-container and profile-name tests pass.
- VPK archive integrity verified; matching full ELF retained beside it.

NVIDIA reference compiler: https://developer.nvidia.com/cg-toolkit-download

## Reproduction

`port/vita/build_vitagl.ps1` applies the tracked patches in `port/vita/patches`
and rebuilds ALL dependency objects (diagnostic flags change struct layout).
The patch baseline is the existing `build/vitagl-reference` source snapshot;
the script refuses a different dependency instead of overwriting it.

```powershell
./port/vita/build_vitagl.ps1
python port/vita/compile_probe.py --sdk sdk-extracted/vitasdk --clang 'C:\Program Files\LLVM\bin\clang.exe' --out build/vita-profile-60 --all --jobs 8
python port/vita/engine_link_probe.py --sdk sdk-extracted/vitasdk --results build/vita-profile-60/compile-results.json --out build/vita-profile-60-link --gpu-libdir gpu-hard/arm-vita-eabi/lib --vita-gl-archive gpu-source/vitaGL-master/libvitaGL.a
```

## Hardware verification still required

Package: `build/vita-profile-60-link/halo-ce-vita.vpk`.
The on-device log remains `ux0:data/halo/boot.log` and must identify
`revision=60 native-cg real-nv2a-vertex-programs indexed-uniforms safe-link`.
It records native shader sources, compiler diagnostics and link results.

No Vita is connected here. Original visual parity, frame rate, audio and level
loading are NOT claimed as verified. Cube/volume sampling and full mip chains
are still incomplete in the Vita backend; this revision targets the proven
crash and the lost vertex/texture-stage processing behind the menu rendering.
