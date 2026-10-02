# Revision 61: missing menu models

The user's revision 60 boot log confirms native Cg compilation and successful
linking for the 2D interface. The model vertex programs (runtime IDs 10 and 48)
instead fail repeatedly with SHARK's diagnostic:

`ambiguous call to 'clamp'` with `(int, int, int)` arguments.

SHARK lists only fixed/half/float candidates. These calls implement the relative
constant addressing used to fetch model bone matrices, e.g.
`c[clamp(a0 + 60, 0, 191)]`. The NVIDIA desktop compiler accepted them, so the
revision 60 host compilation test did not expose the platform incompatibility.

The Vita generator now uses a typed integer helper with comparisons and ternary
operators to clamp the index to [0,191]. No texture, 2D, depth, winding or material
state has been changed speculatively. Linux and Android output is unchanged.

Regression coverage in `tests/test_native_shaders.py` checks the two failing
original programs and executes the generated helper for all valid indices,
negative indices, the first out-of-range index and INT_MIN/INT_MAX. All 67 vertex
programs and 19 fragment-mode cases also pass offline Cg compilation.

Package: `build/vita-profile-61-link/halo-ce-vita.vpk`.
Expected boot marker: `revision=61 native-cg integer-bone-index-fix`.
The same patched VitaGL archive as revision 60 is used. On-device rendering
still requires verification; removing this compiler error does not by itself
prove that every model/material is visually correct.
