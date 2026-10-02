# Revision 66: PCM buffer ownership and shader cache

Revision 65 device logs show queued music with zero audio output errors, but do
not measure DMA buffer ownership. The audio thread reused one PCM buffer even
though sceAudioOutOutput waits for the previous submission and queues the new
one. Use two buffers so the next mix cannot overwrite audio being played.
The host output harness simulates retaining the submitted pointer, verifies that
it remains untouched, and exercises the production audio thread for five blocks.

Keep stream packets and cursors unconsumed until a game frame has been presented
from the main loop. This prevents the short music burst before shader compilation
finishes; startup silence is submitted to keep the audio port serviced. The test
also checks that mixing is gated until frame presentation.

The supplied startup log reaches the frame loop at 7.2 s but compiles shaders
until 38.2 s. Rebuild VitaGL with HAVE_SHADER_CACHE and use the isolated cache
ux0:data/halo/shader_cache_r66. The first boot still compiles; subsequent boots
can load cached shader binaries. Actual device timing remains to be confirmed.

Graphics fixes and the Xbox ADPCM correction are retained. Original Bink movies
remain unimplemented and are not bundled. The supplied shader-probe log is the
old standalone probe path, not the game's current native-Cg path.
