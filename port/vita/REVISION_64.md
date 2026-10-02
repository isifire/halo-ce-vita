# Revision 64: restore missing color and math header functions

The supplied boot log identifies revision 62 and contains pointer-like fog and
final-combiner colors (fog 816ebfec, final 81b7ea94/81b7ea88). Inspection of the
revision 63 ELF confirms the same underlying build defect: 15 game functions
remain undefined weak symbols, including all three packed-color conversions.
The linker silently replaces the fog color conversion call with an ARM NOP;
the following render-state setter receives the input pointer left in r0.

The Vita compile inventory now includes port/linux/game/msvc_comdat.c, as the
desktop port does. On Vita its exported header definitions explicitly have weak
linkage so existing definitions can coexist. This supplies the color conversions
and the missing vector, distance, random, spline and collision helpers.

The packaging step now checks the generated header-inline symbol inventory
against the ELF's undefined symbols. Missing game header functions stop the
build even when GNU ld reports a successful link.

Validation: 514/514 units compiled; full link and VPK packaging succeeded.
The guard rejects the previous revision's missing functions and accepts revision
64. Disassembly now contains a real branch to real_rgb_color_to_pixel32 where
revision 63 had a NOP. VPK CRC and embedded eboot match the new build. The boot
marker is revision=64 linked-color-math-inlines. Visual confirmation still
requires running this VPK on Vita.
