/* SPDX-License-Identifier: APSL-2.0
 * Bounded silent PCM diagnostic using the existing HackiOS audio protocols:
 * https://github.com/oosexgang/hakios-xnu-kernel
 * No physical amplifier or iOS sink is enabled by this diagnostic.
 */
static struct mi8_audio_dma mi8_probe_dma;
static struct mi8_audio_session mi8_probe_session;
static int16_t mi8_probe_silence[MI8_PCM_MAX_BYTES / sizeof(int16_t)];

static int
mi8_probe_amp(__unused void *context)
{
	return 0;
}

static void
mi8_probe_sink(__unused const char *name, __unused mi8_audio_sink_fn sink)
{
	/* Intentionally do not attach this diagnostic to the system audio device. */
}

static void
mi8_audio_dma_probe(struct mi8_apr_client *client)
{
	struct mi8_audio_amplifier fake_amp = { NULL, mi8_probe_amp, mi8_probe_amp };
	struct mi8_audio_session *s = &mi8_probe_session;
	int rc, stop;
	if (mi8_probe_dma.allocation || s->engine_attached || s->needs_reset) {
		say("mi8rp: audio-dma: previous resources retained, not retrying\n");
		return;
	}
	say("mi8rp: audio-dma: allocating tables and sample pages\n");
	rc = mi8_audio_dma_prepare(&mi8_probe_dma);
	if (rc) { say("mi8rp: audio-dma: allocation failed %d\n", rc); return; }
	say("mi8rp: audio-dma: installing SMMU mapping\n");
	rc = mi8_audio_dma_install(&mi8_probe_dma);
	if (rc) { say("mi8rp: audio-dma: mapping failed %d state %u\n", rc, mi8_probe_dma.mapping.state); return; }
	say("mi8rp: audio-dma: mapping installed slot %u context %u; opening stream\n",
	    mi8_probe_dma.mapping.slot, mi8_probe_dma.mapping.context);
	rc = mi8_session_start(s, client, &mi8_probe_dma.mapping,
	    (void *)(mi8_probe_dma.allocation + 3 * MI8_SMMU_PAGE_BYTES), MI8_PCM_BYTES,
	    MI8_AUDIO_DSP_ADDRESS, mi8_audio_dma_sync, NULL, mi8_probe_sink, fake_amp);
	if (rc) {
		say("mi8rp: audio-dma: session start failed %d reset %d\n", rc, s->needs_reset);
		client->event = NULL; client->event_context = NULL;
		return;
	}
	say("mi8rp: audio-dma: sending one silent buffer\n");
	uint32_t frames = mi8_engine_write(&s->engine, mi8_probe_silence, MI8_PCM_MAX_BYTES / MI8_RING_FRAME_BYTES);
	rc = mi8_engine_service(&s->engine);
	for (unsigned i = 0; i < 2000 && s->engine.pcm.inflight; i++) {
		client->poll(client->transport);
		if (s->engine.pcm.inflight) client->sleep_us(client->transport, 1000);
	}
	say("mi8rp: audio-dma: queued %u service %d done %u errors %u inflight %#x\n",
	    frames, rc, s->engine.pcm.completed, s->engine.pcm.errors, s->engine.pcm.inflight);
	stop = mi8_session_stop(s);
	say("mi8rp: audio-dma: session stop %d reset %d\n", stop, s->needs_reset);
	/* The probe's APR client is stack allocated and the link is about to close. */
	client->event = NULL; client->event_context = NULL;
}

/* Only called after TrustZone confirms shutdown. Uncertain SMMU restore keeps
 * the allocation for the rest of this boot. */
static void
mi8_audio_dma_probe_shutdown(bool stopped)
{
	if (!mi8_probe_dma.allocation) return;
	if (!stopped) { say("mi8rp: audio-dma: DSP stop unconfirmed; retaining pages\n"); return; }
	int rc = mi8_audio_dma_release(&mi8_probe_dma);
	say("mi8rp: audio-dma: post-shutdown release %d\n", rc);
}
