/* SPDX-License-Identifier: APSL-2.0
 * Playback lifecycle built around the existing Redwood audio/ASM ordering:
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_AUDIO_SESSION_H
#define MI8_AUDIO_SESSION_H
#include "mi8-audio-sink.h"
#include "mi8-afe.h"
#include "mi8-smmu-program.h"

struct mi8_audio_amplifier {
	void *context;
	/* Must verify device-specific firmware, calibration and protection before
	 * unmuting. Nonzero means failure and is followed by stop. */
	int (*start)(void *);
	int (*stop)(void *);
};

struct mi8_audio_session {
	struct mi8_pcm_engine engine;
	struct mi8_afe_result afe;
	struct mi8_smmu_mapping *smmu;
	struct mi8_audio_amplifier amp;
	mi8_audio_set_sink_fn set_sink;
	uint16_t copp;
	int engine_attached, afe_attempted, adm_open, asm_attempted, amp_attempted, sink_enabled;
	int needs_reset;
};

/* Local/transport errors may mean the DSP applied a command without sending
 * its reply. Explicit DSP rejections (encoded >= 1000) are not ambiguous. */
static inline int
mi8_session_uncertain(int status)
{
	return status != 0 && status != -9 && status < 1000;
}

/* Call after the producer is detached and only on the sole transport worker.
 * A nonzero return requires stopping the DSP before releasing DMA memory or
 * SMMU resources. A successful stop leaves SMMU release to the caller. */
static inline int
mi8_session_stop(struct mi8_audio_session *s)
{
	struct mi8_apr_client *client;
	if (!s || !s->engine_attached || !s->engine.pcm.client) return -1;
	client = s->engine.pcm.client;
	if (s->sink_enabled) {
		if (mi8_audio_sink_disable(&s->engine, s->set_sink)) s->needs_reset = 1;
		else s->sink_enabled = 0;
	}
	if (s->amp_attempted) {
		if (s->amp.stop(s->amp.context)) s->needs_reset = 1;
		else s->amp_attempted = 0;
	}
	if (s->asm_attempted) {
		if (mi8_asm_close(client)) s->needs_reset = 1;
		else s->asm_attempted = 0;
	}
	if (s->adm_open) {
		if (mi8_adm_close(client, s->copp)) s->needs_reset = 1;
		else s->adm_open = 0;
	}
	if (s->afe_attempted) {
		mi8_afe_close(client, &s->afe);
		if (s->afe.clock_off || (s->afe.start != -9 && s->afe.stop)) s->needs_reset = 1;
		else s->afe_attempted = 0;
	}
	/* Give completion events a bounded opportunity to arrive after close. */
	for (unsigned i = 0; s->engine.pcm.inflight && i < 1000; i++) {
		client->poll(client->transport);
		if (s->engine.pcm.inflight) client->sleep_us(client->transport, 1000);
	}
	if (s->engine.pcm.inflight || s->engine.pcm.quarantined) s->needs_reset = 1;
	if (!s->needs_reset && s->engine.pcm.mapped && mi8_pcm_unmap(&s->engine.pcm)) s->needs_reset = 1;
	if (!s->needs_reset && mi8_engine_detach(&s->engine)) s->needs_reset = 1;
	if (!s->needs_reset) s->engine_attached = 0;
	return s->needs_reset ? -1 : 0;
}

/* The caller must zero s, install an exclusive SMMU mapping, flush its page
 * tables, and retain the backing pages until stop succeeds or DSP reset. The
 * caller drives mi8_engine_service and transport polling on the same worker. */
static inline int
mi8_session_start(struct mi8_audio_session *s, struct mi8_apr_client *client,
    struct mi8_smmu_mapping *smmu, void *memory, size_t bytes, uint64_t dsp_address,
    void (*sync)(void *, void *, size_t), void *sync_context,
    mi8_audio_set_sink_fn set_sink, struct mi8_audio_amplifier amp)
{
	struct mi8_asm_result stream;
	int rc;
	if (!s || !client || !smmu || smmu->state != 2 || !set_sink || !amp.start || !amp.stop ||
	    s->engine_attached || s->needs_reset) return -1;
	s->smmu = smmu; s->amp = amp; s->set_sink = set_sink;
	rc = mi8_engine_init(&s->engine, client, memory, bytes, dsp_address, sync, sync_context);
	if (rc) return rc;
	s->engine_attached = 1;
	rc = mi8_pcm_map(&s->engine.pcm);
	if (rc) goto failed;
	s->afe_attempted = 1;
	s->afe = mi8_afe_open(client);
	if (s->afe.clock_on || s->afe.configure || s->afe.start) {
		if (mi8_session_uncertain(s->afe.clock_on) || mi8_session_uncertain(s->afe.configure) ||
		    mi8_session_uncertain(s->afe.start)) s->needs_reset = 1;
		rc = -2; goto failed;
	}
	rc = mi8_adm_open(client, MI8_AFE_PORT, &s->copp);
	if (rc) { if (mi8_session_uncertain(rc)) s->needs_reset = 1; goto failed; }
	s->adm_open = 1;
	s->asm_attempted = 1;
	stream = mi8_asm_start(client, s->copp);
	if (stream.open || stream.route || stream.format || stream.run) {
		if (mi8_session_uncertain(stream.open) || mi8_session_uncertain(stream.route) ||
		    mi8_session_uncertain(stream.format) || mi8_session_uncertain(stream.run)) s->needs_reset = 1;
		rc = -3; goto failed;
	}
	s->amp_attempted = 1;
	if (s->amp.start(s->amp.context)) { rc = -4; goto failed; }
	rc = mi8_audio_sink_enable(&s->engine, set_sink);
	if (rc) goto failed;
	s->sink_enabled = 1;
	return 0;
failed:
	(void)mi8_session_stop(s);
	return rc;
}
#endif
