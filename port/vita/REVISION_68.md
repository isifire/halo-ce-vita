# Revision 68: campaign cache validation and safe assertion reporting

## Device evidence (revision 67)

The supplied boot.log identifies revision 67. Menu graphics and audio are kept
unchanged. The a10 copy starts at 14:46:00 and reports completion about 74 seconds
later. The next boot-log entry is cache_files.c:544:
`'' does not appear to be a cache file`. The old completion line alone does not
prove that every write succeeded or that the destination header is usable.

The supplied psp2core-1790858837-0x00251f2f7b dump is only 4096 compressed bytes.
Partial gzip inflation yields 12453 bytes, including the complete THREAD_INFO
and THREAD_REG_INFO notes, but not module/stack memory. The main thread stopped
with data abort, PC 0x811f1fdc, LR 0x811f1fa0, R0 0xffffffff.
Using the process entry 0x8107af85 and revision-67 ELF entry 0x81000f85 to infer
the 0x7a000 text relocation maps the PC to 0x81177fdc:
`stack_walk_with_context`, instruction `ldr r1, [r0, #-4]`.
This is a secondary crash in the Xbox/x86 assertion-reporting code, not evidence
that the campaign cache failure was caused by the GPU.
Note-layout reference: https://github.com/xyzz/vita-parse-core/blob/master/core.py

## Changes

- Do not interpret x86 stack chains or XDK CONTEXT pointers on Vita. Record the
  limitation and preserve the original assertion in debug.txt/boot.log.
- Never match an empty map request to an invalidated/unused cache slot. Reject
  slots with bad signatures or unterminated names before string comparison.
- Bound the header-name terminator check to its 32-byte field on Vita.
- Re-read copy failure flags after completion synchronization. Previously a
  failure published during the blocking sleep could be reported as success.
- Commit the destination header only after successful payload writes. An
  incomplete copy cannot publish success just because its worker returned.
- Log header signatures/names/lengths, final copy counters, and map-name values
  at the main -> game -> scenario boundaries to locate the initial failure.

## Validation and limits

`test_campaign_cache.py` compiles extracted production functions and exercises
empty/invalid/valid slot selection, completion races, and assertion reporting
with an intentionally invalid XDK context. It does not emulate Vita I/O or load
a real a10 map. Existing texture/audio tests are retained.

This is not a claim that the campaign is playable. The cause of the empty map
name/invalid header in the supplied log is not yet established. A fresh device
run must confirm entry into a10; if it fails, the added boundary and copy records
identify whether the map name is lost or the cache is invalid. Do not delete
the user's profiles, maps, or cache as a debugging shortcut.

Artifact: build/vita-profile-68-link/halo-ce-vita.vpk. Preserve the matching
halo-vita-full.elf for any new core dump. Persistent VitaGL shader cache remains
disabled; revision-67 audio and native graphics paths are unchanged.
