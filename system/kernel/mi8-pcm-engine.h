/* SPDX-License-Identifier: APSL-2.0
 * Playback worker building on the existing Redwood audio and ASM design:
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_PCM_ENGINE_H
#define MI8_PCM_ENGINE_H
#include "mi8-asm-pcm.h"
#include "mi8-pcm-ring.h"

struct mi8_pcm_engine {
	struct mi8_pcm pcm;
	struct mi8_pcm_ring ring;
	int16_t scratch[MI8_PCM_MAX_BYTES / sizeof(int16_t)];
	uint64_t submitted_frames, rejected_frames;
};

/* Engine storage must be zeroed before first use. Initialize and attach before
 * registering an iOS sink. No other event handler may be installed on this
 * client. The caller owns the engine lifetime. */
static inline int
mi8_engine_init(struct mi8_pcm_engine *engine, struct mi8_apr_client *client,
    void *memory, size_t bytes, uint64_t address,
    void (*sync)(void *, void *, size_t), void *sync_context)
{
	int rc;
	if (!engine || !client || engine->pcm.client || client->event) return -1;
	memset(engine, 0, sizeof(*engine));
	rc = mi8_pcm_init(&engine->pcm, client, memory, bytes, address, sync, sync_context);
	if (rc) return rc;
	mi8_ring_init(&engine->ring);
	client->event_context = &engine->pcm;
	client->event = mi8_pcm_receive;
	return 0;
}

/* Called from redwood_audio's serialized sink callback; never performs I/O. */
static inline uint32_t
mi8_engine_write(struct mi8_pcm_engine *engine, const int16_t *samples, uint32_t frames)
{
	return engine ? mi8_ring_write(&engine->ring, samples, frames) : 0;
}

/* Called by the sole transport worker, with the ASM session running. Polling
 * the GLINK client separately delivers completions and makes slots reusable.
 * A negative return stops feeding; ambiguous transport failures quarantine PCM.
 */
static inline int
mi8_engine_service(struct mi8_pcm_engine *engine)
{
	uint32_t submitted = 0;
	if (!engine || !engine->pcm.mapped || engine->pcm.quarantined) return -1;
	if (engine->pcm.errors) return -2;
	for (unsigned slot = 0; slot < MI8_PCM_PERIODS; slot++) {
		uint32_t frames;
		int rc;
		if (engine->pcm.inflight & (1u << slot)) continue;
		frames = mi8_ring_read(&engine->ring, engine->scratch, MI8_PCM_MAX_BYTES / MI8_RING_FRAME_BYTES);
		if (!frames) break;
		rc = mi8_pcm_submit(&engine->pcm, slot, engine->scratch, frames * MI8_RING_FRAME_BYTES);
		if (rc) { engine->rejected_frames += frames; return rc > 0 ? -3 : rc; }
		engine->submitted_frames += frames;
		submitted += frames;
		if (engine->pcm.errors) return -2;
	}
	return (int)submitted;
}

/* Stop the producer and worker before detach; unmap first. */
static inline int
mi8_engine_detach(struct mi8_pcm_engine *engine)
{
	struct mi8_apr_client *client;
	if (!engine || !engine->pcm.client || engine->pcm.mapped || engine->pcm.inflight || engine->pcm.quarantined) return -1;
	client = engine->pcm.client;
	if (client->event != mi8_pcm_receive || client->event_context != &engine->pcm) return -1;
	client->event = NULL; client->event_context = NULL;
	engine->pcm.client = NULL;
	return 0;
}
#endif
