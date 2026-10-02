# Revision 65: Xbox ADPCM blocks and startup timing

The Vita decoder previously discarded the header predictor and emitted all 64
coded nibbles. Xbox playback emits the predictor plus 63 decoded nibbles instead.
Restore that sequence, retaining 64 frames per channel per 36-byte block. Also
use a single multiply/shift for the IMA difference instead of separately rounded
terms. Reference: FFmpeg libavcodec/adpcm.c, ADPCM_IMA_XBOX and
adpcm_ima_expand_nibble:
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c

test_adpcm.py runs the production decoder against known mono and stereo vectors,
checking the predictor, nibble rounding, padding and consecutive block boundaries.
This fixes a decoding defect that can cause periodic noise; device listening is
still needed to establish whether other audio problems remain.

Shell, first-frame and shader compilation checkpoints now include milliseconds
to locate the reported black-screen delay. This revision does not claim to
shorten the delay. Bink remains unsupported (bink_null.c); no original .bik movie
assets were found in the workspace, and no startup movies are bundled.

Revision 64's color/math inline-link fix and packaging guard are retained.
