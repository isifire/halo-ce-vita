/*
 * DSOUND_VITA.C
 *
 * Xbox DirectSound implementation for the PS Vita build.
 * DirectSound software mixer feeding native PS Vita SceAudioOut.
 *
 * Supports:
 * - Xbox 4-bit IMA ADPCM decoding
 * - 16-bit PCM streaming
 * - 3D audio spatialization (distance rolloff, panning, listener orientation, I3DL2)
 * - Dynamic resampling / frequency pitch adjustments
 * - Low-latency output thread targeting sceAudioOutOutput (48 kHz stereo 16-bit PCM)
 */

#include <xtl.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2
#define MAXIMUM_STREAM_PACKETS 64
#define MIX_CHUNK_FRAMES 1024

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

/* ---------- Mutex wrapper using Vita kernel mutex */

static SceUID g_mixer_mutex = -1;

static void mixer_lock(void)
{
	if (g_mixer_mutex < 0)
	{
		g_mixer_mutex = sceKernelCreateMutex("halo_dsound_mutex", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	}
	if (g_mixer_mutex >= 0)
		sceKernelLockMutex(g_mixer_mutex, 1, NULL);
}

static void mixer_unlock(void)
{
	if (g_mixer_mutex >= 0)
		sceKernelUnlockMutex(g_mixer_mutex, 1);
}

/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
};

struct vita_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct vita_stream *next;
	ULONG reference_count;
	BOOL destroying;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	float headroom;

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	float i3dl2_gain;

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* position inside the head packet, in source frames */
	double cursor;
	/* the last frame of the previous packet, for interpolating across packets */
	float previous[2];
	/* gains the mixer is ramping from, to avoid clicks */
	float current_left, current_right;
	BOOL gains_valid;
};

static struct vita_stream *streams = NULL;
static BOOL game_audio_ready = FALSE;

/* Keep queued packets/cursors intact until the first game frame is visible. */
void halo_vita_audio_frame_presented(void)
{
	mixer_lock();
	game_audio_ready = TRUE;
	mixer_unlock();
}

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	float distance_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f };

static float master_volume = 1.0f;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static int ima_expand(int nibble, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = ((2 * (nibble & 7) + 1) * step) >> 3;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

static short *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	short *samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(short));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;
			/* Xbox outputs the header predictor followed by 63 decoded
			 * nibbles, not all 64 nibbles without the predictor. */
			output[channel] = (short)predictor;

			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					unsigned long sample = 1 + group * 8 + byte * 2;

					output[sample * channels + channel] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = malloc((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
		memcpy(samples, source, frames * channels * sizeof(short));
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void spatialize(const struct vita_stream *stream, float *left, float *right)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		ahead = dot3(offset, listener.front);
	}
	distance = sqrtf(dot3(offset, offset)) * listener.distance_factor;

	attenuation = 1.0f;
	if (distance > stream->minimum_distance && stream->minimum_distance > 0.0f)
	{
		float clamped = distance < stream->maximum_distance ? distance : stream->maximum_distance;

		attenuation = stream->minimum_distance /
			(stream->minimum_distance + listener.rolloff_factor * (clamped - stream->minimum_distance));
	}

	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	*left *= attenuation * stream->i3dl2_gain;
	*right *= attenuation * stream->i3dl2_gain;
}

static void voice_gains(const struct vita_stream *stream, float *left, float *right)
{
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		spatialize(stream, left, right);
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	*left *= stream->volume * master_volume;
	*right *= stream->volume * master_volume;
}

/* ---------- mixing */

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

static void mix_voice(struct vita_stream *stream, float *output, unsigned long frames)
{
	double step;
	float target_left, target_right, left, right, ramp_left, ramp_right;
	unsigned long frame;

	if (stream->paused || !stream->packet_count || !stream->sample_rate)
		return;
	step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE;
	voice_gains(stream, &target_left, &target_right);
	if (!stream->gains_valid)
	{
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->gains_valid = TRUE;
	}
	left = stream->current_left;
	right = stream->current_right;
	ramp_left = (target_left - left) / (float)frames;
	ramp_right = (target_right - right) / (float)frames;

	for (frame = 0; frame < frames; frame++)
	{
		struct voice_packet *packet;
		unsigned long index;
		float fraction, sample_left, sample_right;

		for (;;)
		{
			unsigned long position;

			packet = NULL;
			for (position = 0; position < stream->packet_count; position++)
			{
				struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

				if (!candidate->finished)
				{
					packet = candidate;
					break;
				}
			}
			if (!packet)
				break;
			if (stream->cursor < (double)packet->frames)
				break;
			stream->cursor -= (double)packet->frames;
			if (packet->frames)
			{
				unsigned long last = packet->frames - 1;

				stream->previous[0] = packet_sample(packet, last, 0, stream->channels);
				stream->previous[1] = packet_sample(packet, last, stream->channels - 1, stream->channels);
			}
			packet->finished = TRUE;
		}
		if (!packet)
			break;

		index = (unsigned long)stream->cursor;
		fraction = (float)(stream->cursor - (double)index);
		{
			float a0 = packet_sample(packet, index, 0, stream->channels);
			float a1 = packet_sample(packet, index, stream->channels - 1, stream->channels);
			float b0, b1;

			if (index + 1 < packet->frames)
			{
				b0 = packet_sample(packet, index + 1, 0, stream->channels);
				b1 = packet_sample(packet, index + 1, stream->channels - 1, stream->channels);
			}
			else
			{
				b0 = a0;
				b1 = a1;
			}
			sample_left = a0 + (b0 - a0) * fraction;
			sample_right = a1 + (b1 - a1) * fraction;
		}
		if (stream->channels == 1)
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_left * right;
		}
		else
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		stream->cursor += step;
	}
	stream->current_left = target_left;
	stream->current_right = target_right;
}

static void mix(float *output, unsigned long frames, unsigned long *voice_count, unsigned long *packet_count)
{
	struct vita_stream *stream;
	unsigned long sample;

	*voice_count = 0;
	*packet_count = 0;
	memset(output, 0, frames * OUTPUT_CHANNELS * sizeof(float));
	mixer_lock();
	if (!game_audio_ready)
	{
		mixer_unlock();
		return;
	}
	for (stream = streams; stream; stream = stream->next)
	{
		if (!stream->paused && stream->packet_count && stream->sample_rate)
			++*voice_count;
		*packet_count += stream->packet_count;
		mix_voice(stream, output, frames);
	}
	mixer_unlock();

	/* Soft limiting curve */
	for (sample = 0; sample < frames * OUTPUT_CHANNELS; sample++)
	{
		float value = output[sample];

		if (value > 0.8f || value < -0.8f)
		{
			float sign = value < 0.0f ? -1.0f : 1.0f;
			float excess = fabsf(value) - 0.8f;

			output[sample] = sign * (0.8f + 0.2f * tanhf(excess / 0.2f));
		}
	}
}

/* ---------- PS Vita Audio Output Thread */

static SceUID audio_thread_uid = -1;
static int audio_port = -1;
static volatile BOOL audio_running = FALSE;
static BOOL audio_started = FALSE;

#define AUDIO_DIAG_WINDOW_BLOCKS 128
struct audio_diag_window
{
	unsigned long blocks;
	unsigned long samples;
	unsigned long voice_blocks;
	unsigned long packet_blocks;
	unsigned long silent_blocks;
	double sum;
	double absolute_sum;
	double square_sum;
	float peak;
	short minimum;
	short maximum;
	int last_output_result;
	unsigned long output_errors;
};
static struct audio_diag_window audio_diag;

static void audio_log_port_status(const char *event, int result)
{
	FILE *log = fopen("ux0:data/halo/boot.log", "a");
	if (log)
	{
		fprintf(log, "audio_diag: %s result=%d port=%d frames=%d rate=%d channels=stereo\n",
			event, result, audio_port, MIX_CHUNK_FRAMES, OUTPUT_RATE);
		fclose(log);
	}
}

static void audio_diag_flush(void)
{
	struct audio_diag_window window;
	int ready = FALSE;
	FILE *log;

	mixer_lock();
	if (audio_diag.blocks >= AUDIO_DIAG_WINDOW_BLOCKS)
	{
		window = audio_diag;
		memset(&audio_diag, 0, sizeof(audio_diag));
		audio_diag.minimum = 32767;
		audio_diag.maximum = -32768;
		ready = TRUE;
	}
	mixer_unlock();
	if (!ready)
		return;

	log = fopen("ux0:data/halo/boot.log", "a");
	if (log)
	{
		double count = window.samples ? (double)window.samples : 1.0;
		fprintf(log,
			"audio_diag: window_blocks=%lu voices_avg=%.2f packets_avg=%.2f silent_blocks=%lu "
			"pcm_peak=%.5f pcm_abs_avg=%.5f pcm_rms=%.5f pcm_dc=%.5f pcm_min=%d pcm_max=%d "
			"port=%d output_result=%d output_errors=%lu\n",
			window.blocks, (double)window.voice_blocks / (window.blocks ? window.blocks : 1),
			(double)window.packet_blocks / (window.blocks ? window.blocks : 1), window.silent_blocks,
			(double)window.peak, window.absolute_sum / count, sqrt(window.square_sum / count),
			window.sum / count, (int)window.minimum, (int)window.maximum,
			audio_port, window.last_output_result, window.output_errors);
		fclose(log);
	}
}

static int audio_thread_func(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	static float float_buffer[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	/* Output waits for the PREVIOUS submission, then queues this buffer.
	 * Keep the queued samples intact while preparing the next submission. */
	static short pcm_buffers[2][MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	unsigned int pcm_index = 0;
	static unsigned long voice_count, packet_count;

	while (audio_running)
	{
		short *pcm_buffer = pcm_buffers[pcm_index];
		float peak = 0.0f;
		double sum = 0.0, absolute_sum = 0.0, square_sum = 0.0;
		short minimum = 32767, maximum = -32768;
		unsigned long i;
		int output_result;

		mix(float_buffer, MIX_CHUNK_FRAMES, &voice_count, &packet_count);

		for (i = 0; i < MIX_CHUNK_FRAMES * OUTPUT_CHANNELS; i++)
		{
			float val = float_buffer[i];
			short sample;
			if (val > 1.0f) val = 1.0f;
			else if (val < -1.0f) val = -1.0f;
			sample = (short)(val * 32767.0f);
			pcm_buffer[i] = sample;
			if (sample < minimum) minimum = sample;
			if (sample > maximum) maximum = sample;
			val = (float)sample * (1.0f / 32768.0f);
			if (fabsf(val) > peak) peak = fabsf(val);
			sum += val;
			absolute_sum += fabsf(val);
			square_sum += (double)val * val;
		}

		if (audio_port >= 0)
		{
			output_result = sceAudioOutOutput(audio_port, pcm_buffer);
			pcm_index ^= 1;
		}
		else
		{
			output_result = audio_port;
			sceKernelDelayThread((MIX_CHUNK_FRAMES * 1000000) / OUTPUT_RATE);
		}

		/* Collect cheap signal/output metrics here; file I/O is deferred to the
		 * game thread so diagnostics cannot stall the real-time audio producer. */
		mixer_lock();
		if (!audio_diag.samples || minimum < audio_diag.minimum) audio_diag.minimum = minimum;
		if (!audio_diag.samples || maximum > audio_diag.maximum) audio_diag.maximum = maximum;
		if (peak > audio_diag.peak) audio_diag.peak = peak;
		audio_diag.blocks++;
		audio_diag.samples += MIX_CHUNK_FRAMES * OUTPUT_CHANNELS;
		audio_diag.voice_blocks += voice_count;
		audio_diag.packet_blocks += packet_count;
		if (peak == 0.0f) audio_diag.silent_blocks++;
		audio_diag.sum += sum;
		audio_diag.absolute_sum += absolute_sum;
		audio_diag.square_sum += square_sum;
		audio_diag.last_output_result = output_result;
		if (output_result < 0) audio_diag.output_errors++;
		mixer_unlock();
	}
	return 0;
}

static void audio_start(void)
{
	if (audio_started)
		return;
	audio_started = TRUE;
	master_volume = 1.0f;

	audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, MIX_CHUNK_FRAMES, OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
	audio_log_port_status("open_port", audio_port);
	audio_running = TRUE;
	audio_thread_uid = sceKernelCreateThread("halo_dsound_thread", audio_thread_func, 0x10000100 - 10, 0x10000, 0, 0, NULL);
	if (audio_thread_uid >= 0)
	{
		int result = sceKernelStartThread(audio_thread_uid, 0, NULL);
		audio_log_port_status("start_thread", result);
	}
	else
	{
		audio_log_port_status("create_thread", audio_thread_uid);
	}
}

/* ---------- completion */

static void packet_release(struct voice_packet *entry)
{
	free(entry->samples);
	entry->samples = NULL;
}

static void stream_complete_head(struct vita_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		mixer_unlock();
		stream->callback(stream->context, packet.pContext, status);
		mixer_lock();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object);

static void streams_complete_finished(void)
{
	struct vita_stream *stream;

	mixer_lock();
	for (;;) {
		/* A callback may release this voice or another voice. Pin the selected
		 * voice and restart the list search instead of retaining next pointers. */
		for (stream = streams; stream; stream = stream->next)
			if (!stream->destroying && stream->packet_count && stream->packets[stream->packet_head].finished) break;
		if (!stream) break;
		++stream->reference_count;
		stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
		mixer_unlock();
		stream_release(&stream->object);
		mixer_lock();
	}
	mixer_unlock();
}

/* ---------- stream interface */

static struct vita_stream *stream_from_interface(void *stream)
{
	return (struct vita_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct vita_stream *stream = stream_from_interface(object);
	ULONG count;

	mixer_lock();
	count = ++stream->reference_count;
	mixer_unlock();
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct vita_stream *stream = stream_from_interface(object);
	struct vita_stream **link;
	ULONG count;

	mixer_lock();
	if (stream->destroying) { mixer_unlock(); return 0; }
	count = --stream->reference_count;
	if (count) { mixer_unlock(); return count; }
	stream->destroying = TRUE;
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	mixer_unlock();
	stream_flush(object);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct vita_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct vita_stream *stream = stream_from_interface(object);

	mixer_lock();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	mixer_unlock();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct vita_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames = 0;
	short *samples;

	(void)output;
	if (!input)
		return E_INVALIDARG;

	samples = stream->adpcm ?
		decode_adpcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames) :
		decode_pcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames);

	mixer_lock();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		mixer_unlock();
		free(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		stream->cursor = 0.0;
		stream->gains_valid = FALSE;
	}
	stream->packet_count++;
	mixer_unlock();
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct vita_stream *stream = stream_from_interface(object);
	BOOL pinned;

	mixer_lock();
	pinned = !stream->destroying;
	if (pinned) ++stream->reference_count;
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->cursor = 0.0;
	mixer_unlock();
	if (pinned) stream_release(object);
	return S_OK;
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct vita_direct_sound
{
	ULONG reference_count;
};

static struct vita_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	streams_complete_finished();
	audio_diag_flush();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply) { (void)sound; (void)listener_properties; (void)apply; return DS_OK; }

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	mixer_unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	mixer_unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	mixer_unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)sound; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	mixer_unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct vita_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	stream->volume = 1.0f;
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	stream->i3dl2_gain = 1.0f;
	mixer_lock();
	stream->next = streams;
	streams = stream;
	mixer_unlock();
	*result = &stream->object;
	return DS_OK;
}

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct vita_stream *record = stream_from_interface(stream);
	unsigned long active;

	mixer_lock();
	active = record->packet_count != 0;
	mixer_unlock();
	return active;
}

#define STREAM_SETTER(body) \
	struct vita_stream *record = stream_from_interface(stream); \
	mixer_lock(); \
	body; \
	mixer_unlock(); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f)
}

HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct vita_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	mixer_lock();
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	mixer_unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct;

	(void)apply;
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	{
		STREAM_SETTER(record->i3dl2_gain = gain_from_millibels(direct))
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers */

struct null_buffer
{
	ULONG reference_count;
	LPVOID data;
	DWORD size;
	BOOL playing;
};

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct null_buffer *buffer = calloc(1, sizeof(*buffer));

	(void)description;
	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct null_buffer *record = (struct null_buffer *)buffer;
	ULONG count = --record->reference_count;

	if (!count)
		free(record);
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct null_buffer *record = (struct null_buffer *)buffer;

	record->data = data;
	record->size = size;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	(void)reserved1;
	(void)reserved2;
	(void)flags;
	((struct null_buffer *)buffer)->playing = TRUE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	((struct null_buffer *)buffer)->playing = FALSE;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor) { (void)buffer; (void)play_cursor; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length) { (void)buffer; (void)loop_start; (void)loop_length; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch) { (void)buffer; (void)pitch; return DS_OK; }
HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume) { (void)buffer; (void)volume; return DS_OK; }
