/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-audio-sink.h"

static mi8_audio_sink_fn installed_sink;
static void set_sink(const char *name, mi8_audio_sink_fn sink)
{
	assert(name);
	installed_sink = sink;
}

struct fixture { struct mi8_apr_client *client; uint32_t tokens[8]; unsigned writes; uint64_t now; int fail_write; };
static int send_packet(void *context, const void *packet, size_t length)
{
	struct fixture *f = context;
	struct mi8_apr_header h;
	struct { struct mi8_apr_header h; uint32_t words[2]; } reply = { 0 };
	memcpy(&h, packet, sizeof(h));
	assert(length == h.size);
	if (h.opcode == MI8_PCM_WRITE) {
		assert(f->writes < 8);
		f->tokens[f->writes++] = h.token;
		return f->fail_write ? 42 : 0;
	}
	reply.h.field = 0x250; reply.h.size = sizeof(reply); reply.h.from_service = 7; reply.h.token = h.token;
	if (h.opcode == MI8_PCM_MAP) { reply.h.opcode = MI8_PCM_MAP_REPLY; reply.words[0] = 99; }
	else { reply.h.opcode = MI8_APR_BASIC_RESULT; reply.words[0] = h.opcode; }
	mi8_apr_client_receive(f->client, &reply, sizeof(reply));
	return 0;
}
static void poll(void *v) { (void)v; }
static void sleep_us(void *v, uint32_t n) { ((struct fixture *)v)->now += n; }
static uint64_t now_us(void *v) { return ((struct fixture *)v)->now; }
static void sync_mem(void *ctx, void *ptr, size_t n) { (void)ctx; assert(ptr && n && n <= MI8_PCM_MAX_BYTES); }
static void done(struct fixture *f, unsigned slot)
{
	uint64_t addr = 0x110000000ull + (uint64_t)slot * MI8_PCM_STRIDE;
	struct { struct mi8_apr_header h; uint32_t words[4]; } p = { 0 };
	p.h.field = 0x250; p.h.size = sizeof(p); p.h.from_service = 7;
	p.h.opcode = MI8_PCM_WRITE_DONE; p.h.token = f->tokens[slot];
	p.words[0] = (uint32_t)addr; p.words[1] = (uint32_t)(addr >> 32); p.words[2] = 99;
	mi8_apr_client_receive(f->client, &p, sizeof(p));
}
int main(void)
{
	static struct mi8_pcm_engine engine;
	static uint8_t memory[MI8_PCM_BYTES];
	static uint32_t samples[2500];
	struct fixture f = { 0 };
	struct mi8_apr_client c = { .transport = &f, .send = send_packet, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	f.client = &c;
	for (uint32_t i = 0; i < 2500; i++) samples[i] = i + 1;
	assert(mi8_engine_init(&engine, &c, memory, sizeof(memory), 0x110000000ull, sync_mem, NULL) == 0);
	struct mi8_apr_client other = { .transport = &f, .send = send_packet, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	assert(mi8_engine_init(&engine, &other, memory, sizeof(memory), 0x110000000ull, sync_mem, NULL) == -1);
	assert(c.event_context == &engine.pcm && engine.pcm.client == &c);
	assert(mi8_pcm_map(&engine.pcm) == 0);
	assert(mi8_audio_sink_enable(&engine, set_sink) == 0 && installed_sink);
	assert(mi8_audio_sink_enable(&engine, set_sink) == -1);
	assert(installed_sink((const int16_t *)samples, 2500) == 2500);
	assert(mi8_engine_service(&engine) == 2500 && f.writes == 3 && engine.pcm.inflight == 7);
	assert(!memcmp(memory, samples, 960 * 4));
	assert(!memcmp(memory + MI8_PCM_STRIDE, samples + 960, 960 * 4));
	assert(!memcmp(memory + 2 * MI8_PCM_STRIDE, samples + 1920, 580 * 4));
	assert(mi8_engine_detach(&engine) == -1 && mi8_pcm_unmap(&engine.pcm) == -1);
	for (unsigned i = 0; i < 3; i++) done(&f, i);
	assert(engine.pcm.inflight == 0 && engine.pcm.completed == 3);
	assert(mi8_audio_sink_disable(&engine, set_sink) == 0 && !installed_sink);
	assert(mi8_audio_sink_disable(&engine, set_sink) == -1);
	assert(mi8_pcm_unmap(&engine.pcm) == 0 && mi8_engine_detach(&engine) == 0);
	assert(c.event == NULL && c.event_context == NULL);
	puts("PCM engine: iOS queue to ASM slots, completions and teardown passed");
}
