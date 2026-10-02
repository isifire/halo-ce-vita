# Revision 62: material blending

Revision 61 renders the menu geometry and 2D images on the user's Vita, but the
ring is over-saturated green/blue, Threshold is dark and the background is blue.
No new revision 61 boot log was supplied with the photo; these symptoms alone
do not prove a unique cause.

Code comparison found a definite state-translation error: the Vita backend
treated numeric zero blend factors as missing and substituted SRCALPHA or
INVSRCALPHA. Xbox D3DBLEND_ZERO is exactly zero. Both the immediate state setter
and per-draw refresh had this bug, affecting replacement and multiplicative
material passes. They now share one implementation that preserves ZERO.

The shared implementation also applies ADD, SUBTRACT, REVSUBTRACT, MIN and MAX
equations, previously ignored. Default device factors are explicitly ONE/ZERO.
Undeclared vertex inputs now use the current SetVertexData register values,
as the Linux backend does, with default w=1 initialized on device creation.

`test_blend_state.py` executes the production translation against recorded GL
calls, verifying both ZERO positions, replacement/multiplication, all five
equations and disabling blending. Link-failure and texture-layout tests pass.

The first 48 model draws additionally log `native_model:` lines containing
shader identity, blend factors/equation, fog/constants and each bound texture's
raw format, dimensions and GL object. This is bounded diagnostics to separate
remaining texture-target issues from material constants and blending.

No global color filter, tint workaround or texture decoder change was made.
Cube/volume sampling remains incomplete; the picture does not establish that
those targets are used in the affected passes. Hardware validation of the color
correction remains necessary. Signed Xbox blend equations and constant blend
color factors also remain unsupported in this backend.

Artifact: `build/vita-profile-62-link/halo-ce-vita.vpk`.
Log marker: `revision=62 zero-blend-factor-fix` in `ux0:data/halo/boot.log`.
