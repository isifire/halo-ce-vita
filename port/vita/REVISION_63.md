# Revision 63: constant blend factors

The new on-device report says stars now render but appear over-bright, the ring
shifts between blue and yellow, and Threshold is too dark. Revision 62 had
already identified one remaining gap: Xbox constant-color/constant-alpha blend
factors were falling through to `ONE` on Vita. This is directly relevant to
Halo's screen-effect and transparent-geometry paths, which use
`D3DBLEND_CONSTANTCOLOR`, `D3DBLEND_INVCONSTANTCOLOR`, and
`D3DBLEND_CONSTANTALPHA`.

The GXM blend API used by VitaGL has no constant-color factors and no
`glBlendColor`; passing those GL tokens would itself fail and could not produce
an exact translation. The Vita path now uses source color/alpha as the closest
available equivalents instead of silently treating all four Xbox factors as
`ONE`. This is exact for screen-flash cases where the source shader emits the
same tint as `D3DRS_BLENDCOLOR`, but remains an approximation for general
materials (including tinted meters). The bounded `native_model:` trace includes
`blendcolor=` to help identify such draws in a fresh device log.

The production blend-state harness covers the four fallback mappings in addition
to the earlier ZERO/equation regressions. Hardware validation is still needed to
judge visual impact; true constant-color blending would require a framebuffer
compositing implementation beyond VitaGL's exposed GXM blend factors.
