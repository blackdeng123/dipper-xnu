/* SPDX-License-Identifier: APSL-2.0
 * Sink registration follows redwood_audio.c and redwood_sound.c:
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_AUDIO_SINK_H
#define MI8_AUDIO_SINK_H
#include "mi8-pcm-engine.h"

typedef uint32_t (*mi8_audio_sink_fn)(const int16_t *, uint32_t);
typedef void (*mi8_audio_set_sink_fn)(const char *, mi8_audio_sink_fn);

/* Only one registered sink, with register/unregister serialized by the owner.
 * redwood_audio calls its sink under audio_lock and set_sink takes that lock.
 * The engine remains alive from before register until after unregister returns.
 */
static struct mi8_pcm_engine *mi8_audio_current_engine;

static inline uint32_t
mi8_audio_sink_write(const int16_t *samples, uint32_t frames)
{
	return mi8_engine_write(mi8_audio_current_engine, samples, frames);
}

/* The caller owns the existing sink and must have started AFE/ADM/ASM and
 * mapped PCM memory before enabling this callback. No DSP I/O occurs here. */
static inline int
mi8_audio_sink_enable(struct mi8_pcm_engine *engine, mi8_audio_set_sink_fn set_sink)
{
	if (!engine || !set_sink || mi8_audio_current_engine || !engine->pcm.client ||
	    !engine->pcm.mapped || engine->pcm.quarantined || engine->pcm.errors ||
	    engine->pcm.client->event != mi8_pcm_receive ||
	    engine->pcm.client->event_context != &engine->pcm) return -1;
	mi8_audio_current_engine = engine;
	set_sink("Mi 8 audio DSP", mi8_audio_sink_write);
	return 0;
}

/* Call this before stopping the worker or freeing its memory. */
static inline int
mi8_audio_sink_disable(struct mi8_pcm_engine *engine, mi8_audio_set_sink_fn set_sink)
{
	if (!engine || !set_sink || mi8_audio_current_engine != engine) return -1;
	set_sink("nothing", NULL);
	mi8_audio_current_engine = NULL;
	return 0;
}
#endif
