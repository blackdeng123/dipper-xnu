/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mi8-asm-pcm.h"

struct fixture {
	struct mi8_apr_client *client;
	struct mi8_apr_header last;
	uint32_t payload[8];
	uint64_t now;
	int syncs, map_reject, send_failure, immediate_done;
};

static int send_packet(void *ctx, const void *data, size_t length)
{
	struct fixture *f = ctx;
	struct { struct mi8_apr_header h; uint32_t words[2]; } reply = { 0 };
	assert(length >= sizeof(f->last));
	memcpy(&f->last, data, sizeof(f->last));
	assert(length == f->last.size);
	memset(f->payload, 0, sizeof(f->payload));
	if (length - sizeof(f->last) <= sizeof(f->payload))
		memcpy(f->payload, (const uint8_t *)data + sizeof(f->last), length - sizeof(f->last));
	if (f->send_failure) return 42;
	if (f->last.opcode == MI8_PCM_WRITE) {
		if (f->immediate_done) {
			struct { struct mi8_apr_header h; uint32_t word[4]; } done = { 0 };
			done.h.field = 0x250; done.h.size = sizeof(done); done.h.from_service = 7;
			done.h.token = f->last.token; done.h.opcode = MI8_PCM_WRITE_DONE;
			done.word[0] = f->payload[0]; done.word[1] = f->payload[1]; done.word[2] = f->payload[2];
			mi8_apr_client_receive(f->client, &done, sizeof(done));
		}
		return 0;
	}
	reply.h.field = 0x250; reply.h.size = sizeof(reply); reply.h.from_service = 7;
	reply.h.token = f->last.token;
	if (f->last.opcode == MI8_PCM_MAP && !f->map_reject) {
		reply.h.opcode = MI8_PCM_MAP_REPLY; reply.words[0] = 0x444;
	} else {
		reply.h.opcode = MI8_APR_BASIC_RESULT; reply.words[0] = f->last.opcode;
		reply.words[1] = f->map_reject ? 2 : 0;
	}
	mi8_apr_client_receive(f->client, &reply, sizeof(reply));
	return 0;
}
static void poll(void *ctx) { (void)ctx; }
static void sleep_us(void *ctx, uint32_t n) { ((struct fixture *)ctx)->now += n; }
static uint64_t now_us(void *ctx) { return ((struct fixture *)ctx)->now; }
static void sync_mem(void *ctx, void *memory, size_t size)
{
	struct fixture *f = ctx;
	assert(memory && size == 16);
	f->syncs++;
}
static void complete(struct fixture *f, uint32_t token, uint64_t address, uint32_t handle, uint32_t status)
{
	struct { struct mi8_apr_header h; uint32_t word[4]; } p = { 0 };
	p.h.field = 0x250; p.h.size = sizeof(p); p.h.from_service = 7;
	p.h.token = token; p.h.opcode = MI8_PCM_WRITE_DONE;
	p.word[0] = (uint32_t)address; p.word[1] = (uint32_t)(address >> 32);
	p.word[2] = handle; p.word[3] = status;
	mi8_apr_client_receive(f->client, &p, sizeof(p));
}
int main(void)
{
	uint8_t memory[MI8_PCM_BYTES], samples[16];
	struct fixture f = { 0 };
	struct mi8_apr_client c = { .transport = &f, .send = send_packet, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	struct mi8_pcm p;
	uint32_t first, second;
	memset(samples, 0x5a, sizeof(samples)); f.client = &c;
	assert(mi8_pcm_init(&p, &c, memory, sizeof(memory), 0x110000001ull, sync_mem, &f) == -1);
	assert(mi8_pcm_init(&p, &c, memory, sizeof(memory), 0x110000000ull, sync_mem, &f) == 0);
	c.event = mi8_pcm_receive; c.event_context = &p;
	assert(mi8_pcm_map(&p) == 0 && p.mapped && p.handle == 0x444);
	assert(mi8_pcm_submit(&p, 2, samples, sizeof(samples)) == 0);
	assert(f.syncs == 1 && !memcmp(memory + 2 * MI8_PCM_STRIDE, samples, sizeof(samples)));
	assert(f.last.opcode == MI8_PCM_WRITE && f.payload[0] == 0x10002000 && f.payload[1] == 1 &&
	    f.payload[2] == 0x444 && f.payload[3] == 16);
	first = f.last.token;
	assert(mi8_pcm_submit(&p, 2, samples, sizeof(samples)) == -2 && mi8_pcm_unmap(&p) == -1);
	complete(&f, first, 0x110003000ull, 0x444, 0);
	complete(&f, first, 0x110002000ull, 0x445, 0);
	assert(p.inflight == 4 && p.completed == 0);
	complete(&f, first, 0x110002000ull, 0x444, 0);
	assert(!p.inflight && p.completed == 1);
	assert(mi8_pcm_submit(&p, 2, samples, sizeof(samples)) == 0);
	second = f.last.token; assert(second != first);
	complete(&f, first, 0x110002000ull, 0x444, 0);
	assert(p.inflight == 4 && p.completed == 1);
	complete(&f, second, 0x110002000ull, 0x444, 3);
	assert(!p.inflight && p.completed == 2 && p.errors == 1);
	assert(mi8_pcm_unmap(&p) == 0 && !p.mapped);
	assert(mi8_pcm_init(&p, &c, memory, sizeof(memory), 0x110000000ull, sync_mem, &f) == 0);
	assert(mi8_pcm_map(&p) == 0);
	f.immediate_done = 1;
	assert(mi8_pcm_submit(&p, 0, samples, sizeof(samples)) == 0 && !p.inflight && p.completed == 1 && f.last.token != second);
	f.immediate_done = 0;
	f.send_failure = 1;
	assert(mi8_pcm_submit(&p, 0, samples, sizeof(samples)) == 42 && p.quarantined);
	assert(mi8_pcm_unmap(&p) == -1);
	assert(mi8_pcm_init(&p, &c, memory, sizeof(memory), 0x110000000ull, sync_mem, &f) == 0);
	f.send_failure = 0; f.map_reject = 1;
	assert(mi8_pcm_map(&p) == 1002 && !p.mapped && !p.quarantined);
	puts("ASM PCM map, slot ownership, stale events and ambiguous send passed");
}
