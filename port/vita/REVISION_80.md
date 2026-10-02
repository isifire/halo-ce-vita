# Revision 80: preserve script/global entry handles during relocation

## Evidence captured directly from the Vita

Read-only FTP retrieval from `192.168.1.162:1337`:

- `ux0:data/halo/boot.log` identifies revision 79 and stops on
  `script node index #7708 (0x857f1e1c) is unused or changed`.
- The requested identifier is `857f`, while slot 7708 contains `818f`.
- `debug.txt` gives runtime `write_to_error_file=0x811af6d8`. The matching
  rev79 ELF symbol is `0x8117c6d8`, so this run's text delta is **0x33000**,
  not the preceding dump's 0x35000. Caller `0x81168224` maps to
  `0x81135224`, in `hs_evaluate`.
- Captured 14,120,584 bytes of a10 tags from cache000.map offset 0x0f885200.
  The previously present local a10-tags.bin is a different size and was NOT
  substituted for the device's capture.
- Captured 1,650,224 bytes of UI tags from cache002.map offset 0x01e75c00.

Local evidence is retained in `build/vita-r80-evidence` and
`build/vita-r80-ui-evidence`. These proprietary map data are not bundled in
the VPK or tests.

## Root cause

The generic cache relocation scans arbitrary words for Xbox address ranges.
Revision 78 protected compiled syntax nodes, but not the scenario's script
and global records that store entry-point datum handles. Those handles can
numerically overlap Xbox addresses.

In the real a10 capture:

| Script | Valid original handle | Corrupted rev79 value |
| --- | --- | --- |
| bridge_flavor | 0x805f1cec | 0x844f1cec |
| bridge_flavor_cycle | 0x818f1e1c | 0x857f1e1c |

The latter root is at tag-image offset 0x5484a0. Adding the run's tag delta
0x03f00000 reproduces the exact reported invalid handle. This is loading-time
corruption, not an invalid original script or a reason to skip its execution.

## Fix and tests

The Vita scanner now accepts validated exclusion ranges. Before relocation,
it resolves the syntax pool, complete script records, complete global records,
and script string bytes. They contain values, not pointers, and remain intact.
The scenario block pointers themselves still relocate. Range validation uses
division before multiplication and rejects nonempty out-of-bounds ranges
instead of truncating them. The revision-79 ARM stack fix remains intact.

`test_hs_rebase_arm.py` compiles the production cache source with the actual
engine headers into ARM code and executes the full relocation in Unicorn.
Only logging and physical-memory-base queries are stubbed. A negative control
protects syntax alone (the old behavior) and reproduces both corruptions.

Executed results:

- a10: all **269 script/global roots** retain valid node identifiers;
  **18,735 nodes** and all protected records/string bytes remain unchanged.
- UI: its **one root and 83 nodes** remain valid/unchanged.
- Both tests confirm the owning block pointers relocate to the Vita base.
- `test_hs_stack_arm.py`: old-path negative control, aligned nested/resumed
  allocations and overflow rejection still pass.
- Full build: **514/514**, final link **zero unresolved symbols**.
- VPK CRC verified; packaged eboot matches the retained eboot.

Reproduction:

```powershell
python port/vita/tests/test_hs_rebase_arm.py --tags build/vita-r80-evidence/tags.bin --negative-control
python port/vita/tests/test_hs_rebase_arm.py --tags build/vita-r80-ui-evidence/tags.bin
python port/vita/tests/test_hs_stack_arm.py
```

Unicorn and pyelftools are required; this workspace has Unicorn under
`build/debug-python`. The tests do not emulate Vita OS/GXM or execute an
entire campaign. Generic heuristic relocation elsewhere is still a risk;
this change does not claim a complete typed-pointer loader.

## Delivery and remaining hardware check

Local VPK: `build/vita-profile-80-link/halo-ce-vita.vpk`.
SHA256: `66081b5b0edba9f0487802bca7494e09c67b613c7be34926d8a7fdfff29b76c0`.
Uploaded as a NEW file `ux0:data/halo/halo-ce-vita-v80.vpk`; downloaded it
again and verified the same SHA256. No installed executable, saved game, map
or device log was overwritten. Matching ELF is retained beside the VPK.

User installation and a fresh mission launch are still needed. FTP provides
files, not controller access or on-device execution. Full gameplay is not
yet verified. Reopen VitaShell FTP after the test to retrieve its result.
