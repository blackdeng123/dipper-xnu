/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-audio-session.h"

struct fixture {
	struct mi8_apr_client *client;
	uint32_t writes[8]; unsigned write_count, maps, unmaps, amp_starts, amp_stops;
	uint64_t now; int fail_amp, timeout_adm;
};
static mi8_audio_sink_fn sink;
static void set_sink(const char *name, mi8_audio_sink_fn callback) { assert(name); sink = callback; }
static int send_packet(void *context, const void *packet, size_t length)
{
	struct fixture *f = context;
	struct mi8_apr_header h;
	struct { struct mi8_apr_header h; uint32_t words[2]; } reply = { 0 };
	memcpy(&h, packet, sizeof(h)); assert(length == h.size);
	if (h.opcode == MI8_PCM_WRITE) { assert(f->write_count < 8); f->writes[f->write_count++] = h.token; return 0; }
	if (h.opcode == MI8_ADM_OPEN && f->timeout_adm) return 0;
	reply.h.field = 0x250; reply.h.size = sizeof(reply); reply.h.token = h.token; reply.h.from_service = h.to_service;
	if (h.opcode == MI8_PCM_MAP) { f->maps++; reply.h.opcode = MI8_PCM_MAP_REPLY; reply.words[0] = 99; }
	else if (h.opcode == MI8_ADM_OPEN) { reply.h.opcode = MI8_ADM_OPEN_REPLY; reply.words[1] = 7; }
	else { if (h.opcode == MI8_PCM_UNMAP) f->unmaps++; reply.h.opcode = MI8_APR_BASIC_RESULT; reply.words[0] = h.opcode; }
	mi8_apr_client_receive(f->client, &reply, sizeof(reply));
	return 0;
}
static void poll(void *v) { (void)v; }
static void sleep_us(void *v, uint32_t n) { ((struct fixture *)v)->now += n; }
static uint64_t now_us(void *v) { return ((struct fixture *)v)->now; }
static void sync_mem(void *v, void *p, size_t n) { (void)v; assert(p && n); }
static int amp_start(void *v) { struct fixture *f = v; f->amp_starts++; return f->fail_amp; }
static int amp_stop(void *v) { ((struct fixture *)v)->amp_stops++; return 0; }
static void complete(struct fixture *f, unsigned slot)
{
	uint64_t address = 0x110000000ull + (uint64_t)slot * MI8_PCM_STRIDE;
	struct { struct mi8_apr_header h; uint32_t words[4]; } event = { 0 };
	event.h.field = 0x250; event.h.size = sizeof(event); event.h.from_service = 7;
	event.h.opcode = MI8_PCM_WRITE_DONE; event.h.token = f->writes[slot];
	event.words[0] = (uint32_t)address; event.words[1] = (uint32_t)(address >> 32); event.words[2] = 99;
	mi8_apr_client_receive(f->client, &event, sizeof(event));
}
int main(void)
{
	static struct mi8_audio_session session;
	static uint8_t memory[MI8_PCM_BYTES];
	static int16_t samples[1280 * 2];
	struct fixture f = { 0 };
	struct mi8_apr_client c = { .transport = &f, .send = send_packet, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	struct mi8_smmu_mapping mapping = { .state = 2 };
	struct mi8_audio_amplifier amp = { &f, amp_start, amp_stop };
	f.client = &c;
	assert(mi8_session_start(&session, &c, &mapping, memory, sizeof(memory), 0x110000000ull,
	    sync_mem, NULL, set_sink, amp) == 0);
	assert(sink && f.maps == 1 && f.amp_starts == 1);
	assert(sink(samples, 1280) == 1280 && mi8_engine_service(&session.engine) == 1280 && f.write_count == 2);
	complete(&f, 0); complete(&f, 1);
	assert(mi8_session_stop(&session) == 0 && !sink && f.unmaps == 1 && f.amp_stops == 1 && !c.event);
	memset(&session, 0, sizeof(session)); f.fail_amp = 1;
	assert(mi8_session_start(&session, &c, &mapping, memory, sizeof(memory), 0x110000000ull,
	    sync_mem, NULL, set_sink, amp) == -4);
	assert(!sink && f.unmaps == 2 && f.amp_stops == 2 && !session.needs_reset && !c.event);
	memset(&session, 0, sizeof(session)); f.fail_amp = 0; f.timeout_adm = 1;
	assert(mi8_session_start(&session, &c, &mapping, memory, sizeof(memory), 0x110000000ull,
	    sync_mem, NULL, set_sink, amp) == -3);
	assert(session.needs_reset && session.engine.pcm.mapped && c.event && f.unmaps == 2 && !sink);
	static struct mi8_audio_session pending;
	static uint8_t pending_memory[MI8_PCM_BYTES];
	struct fixture f2 = { 0 };
	struct mi8_apr_client c2 = { .transport = &f2, .send = send_packet, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	struct mi8_audio_amplifier amp2 = { &f2, amp_start, amp_stop };
	f2.client = &c2;
	assert(mi8_session_start(&pending, &c2, &mapping, pending_memory, sizeof(pending_memory), 0x110000000ull,
	    sync_mem, NULL, set_sink, amp2) == 0);
	assert(sink(samples, 960) == 960 && mi8_engine_service(&pending.engine) == 960);
	/* No WRITE_DONE: the mapping must remain until the DSP has been reset. */
	assert(mi8_session_stop(&pending) == -1 && pending.needs_reset && pending.engine.pcm.mapped &&
	    pending.engine.pcm.inflight && f2.unmaps == 0 && c2.event && !sink);
	puts("Audio session: PCM-to-sink lifecycle, amp failure and ambiguous ADM timeout passed");
}
