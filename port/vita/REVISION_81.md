# Revision 81: clamp out-of-range color components during shader/geometry rasterization

## Evidence captured directly from the Vita

Read-only FTP retrieval from `192.168.1.162:1337`:

- In Revision 80, map mounting and script syntax/record relocation succeeded:
  `cached_map_files_find_map: matched 'a10' in slot 0 (name='levels\a10\a10', bytes=181514240)`
  `cache_file_open: scenario='levels\a10\a10' resolved_index=0`
- Shader programs compiled and linked successfully in VitaGL:
  `vita_combiner: link=1 modes=00000023 combiner_count=69891`
- The game halted during the first frames of scenario rasterization:
  ```text
  ASSERTION FATAL in ..\bitmaps\bitmaps_inlines.h:89: color: assert_valid_real_argb_color(1.000000, 0.000000, 0.000000, 1391.000000)
  system_exit: exit requested with code -1
  halt_and_catch_fire: halting execution
  ```

## Root Cause

`real_argb_color_to_pixel32`, `real_rgb_color_to_pixel32`, and `real_alpha_to_pixel32` in `source/bitmaps/bitmaps_inlines.h` contained assertions enforcing color components in the `[0.0, 1.0]` range.
In retail Xbox Halo CE builds (`HALO_RELEASE`), these assertions are compiled out, and out-of-range/HDR colors (such as blue=1391.0 from level geometry, lighting or shader constants) are naturally saturated/scaled when packed into 8-bit channels. On debug/port builds, however, `valid_real_argb_color` fired a fatal assertion exit.

## Fix

In `source/bitmaps/bitmaps_inlines.h`:
- Added `#ifdef HALO_VITA` clamping of `alpha`, `red`, `green`, and `blue` components to `[0.0f, 1.0f]` before pixel conversion.
- Replaced the failing assertion with clamped conversion in `real_argb_color_to_pixel32`, `real_rgb_color_to_pixel32`, and `real_alpha_to_pixel32`.
- Updated revision banner in `port/vita/src/d3d8_vitagl.c` to `revision=81 clamp-argb-color`.

## Verification & Build

- Compilation: **514/514** units compiled clean (`build/vita-profile-81`).
- Link: **0 unresolved symbols**, partial and SDK link exited with code 0 (`build/vita-profile-81-link`).
- Package: `build/vita-profile-81-link/halo-ce-vita.vpk` (SHA256: `231C57B72EBC3EFA6473B7FEBB9DFE96E2AE113F9D5B830F7D1B187130ED4D12`).
- Deployed via FTP to `ux0:data/halo/halo-ce-vita-v81.vpk`.
