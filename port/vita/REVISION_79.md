# Revision 79: campaign HS stack alignment fault

## Evidence, not a renderer hypothesis

Input: `psp2core-1790927700-0x00007f2051-eboot.bin.psp2dmp`,
Desktop boot.log (revision 78) and debug.txt supplied on 2026-10-02.
The dump is a gzip-compressed ELF, not a raw ELF.

The dump MODULE_INFO maps application text at `0x81035000`; the retained
revision-78 ELF text starts at `0x81000000`. Text relocation is **0x35000**.
Data has a DIFFERENT relocation: runtime `0x81400000`, ELF `0x81390000`.
Do not symbolize runtime PCs directly or apply the text delta to data.

The main thread THREAD_REG_INFO records:

| Item | Value |
| --- | --- |
| Runtime PC | 0x8116ccb8 |
| ELF PC | 0x81137cb8 |
| Function | hs_evaluate_arithmetic + 0xb0 |
| Instruction | `ed980a00`, `vldr s0, [r8]` |
| r8 / fault address | 0x86674f1e |
| LR (runtime) | 0x8116c1b8 |
| Data fault status | 1 (alignment) |

The captured memory includes this thread, at `0x86674e70`. Its current HS
frame pointer at thread+16 is `0x86674f0a`, already misaligned. That frame's
data starts at +14, `0x86674f18`. Arithmetic allocates short/long/real/real
locals contiguously, yielding `0x86674f18`, `0x86674f1a`, **0x86674f1e**,
`0x86674f22`. The value at the fault address is a valid float, 30.0
(`00 00 f0 41`): this is not a null pointer or a missing map texture.

Xbox x86 tolerates this byte-packed runtime stack. ARM VFP requires the
float address to be word-aligned. The generic tag rebase exclusion from
revision 78 remains in place; the previous script-node assertion is absent
from this run. The checksum warnings are not the instruction fault here.

## Correction

Vita-only changes in `source/hs/hs_runtime.c`:

- Align each allocation's absolute address to four bytes. Rounding only the
  requested size is insufficient because `frame->data` begins at offset 14.
- Include padding in both bounds checking and the saved allocation cursor.
- Align nested frame addresses independently, including boolean/short tails.
- Adjust the diagnostic reader of the first local to use the aligned address.
- Preserve Xbox behavior, thread size (0x218), frame struct size, tag data,
  render code and audio behavior. The renderer change is only the revision label.

The runtime stack layout changes on Vita. Existing suspended stacks from
older revisions are not migrated; validation must start a new mission rather
than assume an old checkpoint is compatible. No user save files were deleted.

## Executed validation

`python port/vita/tests/test_hs_stack_arm.py`

The test extracts production stack functions, compiles them for ARM32 and
executes their instructions in Unicorn. It asserts the actual 16-byte frame
and 536-byte thread ABI. The Xbox path is a negative control and fails the
alignment check; the Vita path passes mixed locals, eight nested frames,
replayed allocations with preserved values, an odd-sized tail, exact stack
capacity and rejection of an overflowing allocation.

This is CPU-level testing, NOT a Vita/GXM emulator or full mission execution.

Also passed existing host tests: texture layout, save container, profile name
availability, audio output, ADPCM, blending and VitaGL link guard.
`test_campaign_cache.py` is currently stale: its fake structs/APIs predate
direct-map mounting and it fails to compile. It is not counted as passing.

Build: 514/514 translation units compiled, final link zero unresolved symbols.
VPK: `build/vita-profile-79-link/halo-ce-vita.vpk`.
SHA256: `512418738BC2E11A49C7F95DDA22D36670C1447F6BE8B5FA35BAC8A05EB40112`.
Matching ELF retained in the same directory.

## Remaining boundary

No physical Vita is connected to this environment. Full level loading,
first controllable frame and sustained gameplay are not verified by these
tests. Do not label this build playable until those hardware checks pass.
If another failure occurs, symbolize its own dump against the matching ELF
with that dump's segment relocations; do not return to speculative renderer
or tag changes merely because the last log line mentions a texture.
