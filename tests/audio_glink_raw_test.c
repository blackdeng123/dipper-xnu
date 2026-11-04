/* SPDX-License-Identifier: MIT */
/* Host-side GLINK handshake and raw channel regression test. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mi8_glink.h"

enum { DESC = 0x100, TX = 0x1000, RX = 0x2000, FIFO = 4096 };
struct fixture { uint8_t memory[0x4000]; uint64_t now; unsigned received; };

static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t n) { p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24; }
static bool item(void *c, uint32_t id, uint32_t *at, uint32_t *size) {
	(void)c;
	if (id == 478) { *at = DESC; *size = 32; return true; }
	if (id == 479) { *at = TX; *size = FIFO; return true; }
	if (id == 480) { *at = RX; *size = FIFO; return true; }
	return false;
}
static void read_mem(void *c, uint32_t at, void *out, uint32_t n) {
	struct fixture *f = c; assert(at + n <= sizeof(f->memory)); memcpy(out, f->memory + at, n);
}
static void write32(void *c, uint32_t at, uint32_t n) {
	struct fixture *f = c; assert(at + 4 <= sizeof(f->memory)); put32(f->memory + at, n);
}
static void ring(void *c) { (void)c; }
static void sleep_us(void *c, uint32_t n) { ((struct fixture *)c)->now += n; }
static uint64_t now_us(void *c) { return ((struct fixture *)c)->now; }
static void received(void *c, const void *data, size_t n) {
	struct fixture *f = c; assert(n == 3 && memcmp(data, "OK!", 3) == 0); f->received++;
}
static void push(struct fixture *f, uint16_t cmd, uint16_t cid, uint32_t arg, const void *extra, uint32_t n) {
	uint32_t head = get32(f->memory + DESC + 12), total = (8 + n + 7) & ~7u;
	assert(head + total < FIFO);
	uint8_t *p = f->memory + RX + head;
	memset(p, 0, total); p[0] = cmd; p[1] = cmd >> 8; p[2] = cid; p[3] = cid >> 8;
	put32(p + 4, arg); if (n) memcpy(p + 8, extra, n);
	put32(f->memory + DESC + 12, head + total);
}
static struct mi8_glink *new_link(struct fixture *f, bool raw) {
	struct mi8_glink_cfg c = { .name = "test", .remote_pid = 2, .smem_item = item,
	    .smem_read = read_mem, .smem_write32 = write32, .ring = ring, .sleep_us = sleep_us,
	    .now_us = now_us, .ctx = f, .raw_channel = raw ? "apr_audio_svc" : NULL,
	    .raw_receive = raw ? received : NULL };
	return mi8_glink_create(&c);
}
static void handshake(struct fixture *f, struct mi8_glink *g, const char *name, uint16_t remote) {
	push(f, 1, 1, 1, NULL, 0); /* VERSION_ACK */
	push(f, 2, remote, (uint32_t)strlen(name) + 1, name, (uint32_t)strlen(name) + 1);
	mi8_glink_poll_once(g);
	push(f, 4, 1, 0, NULL, 0); /* OPEN_ACK */
	mi8_glink_poll_once(g);
}
int main(void) {
	struct fixture f = { 0 };
	struct mi8_glink *g = new_link(&f, true);
	uint8_t intent[65 * 8], packet[11];
	assert(g != NULL && mi8_glink_start(g) == 0);
	assert(mi8_glink_qrtr_ops(g) == NULL);
	push(&f, 2, 3, 7, "IPCRTR", 7); /* ignored in APR mode */
	handshake(&f, g, "apr_audio_svc", 5);
	/* The real DSP advertises at least 65 buffers, starting at ID zero. */
	for (unsigned i = 0; i < 65; i++) {
		put32(intent + i * 8, i == 64 ? 512 : 128);
		put32(intent + i * 8 + 4, i);
	}
	push(&f, 5, 5, 65, intent, sizeof(intent));
	mi8_glink_poll_once(g);
	assert(mi8_glink_raw_send(g, "TEST", 4) == 0);
	uint32_t txend = get32(f.memory + DESC + 4);
	bool found = false;
	for (uint32_t i = 0; i + 20 <= txend; i += 8)
		if (f.memory[TX + i] == 9 && f.memory[TX + i + 2] == 1 &&
		    get32(f.memory + TX + i + 4) == 0 && memcmp(f.memory + TX + i + 16, "TEST", 4) == 0) found = true;
	assert(found);
	put32(packet, 3); put32(packet + 4, 0); memcpy(packet + 8, "OK!", 3);
	push(&f, 9, 5, 1, packet, sizeof(packet));
	mi8_glink_poll_once(g);
	assert(f.received == 1);
	mi8_glink_destroy(g);
	memset(&f, 0, sizeof(f));
	g = new_link(&f, false);
	assert(g != NULL && mi8_glink_start(g) == 0);
	handshake(&f, g, "IPCRTR", 5);
	assert(mi8_glink_qrtr_ops(g) != NULL);
	assert(mi8_glink_raw_send(g, "X", 1) == EINVAL);
	mi8_glink_destroy(g);
	puts("raw APR and original QRTR GLINK paths passed");
	return 0;
}
