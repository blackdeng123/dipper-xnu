/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-apr-client.h"

struct fixture { struct mi8_apr_client *client; uint64_t now; int mode, polls; struct mi8_apr_header request; };
static int send_packet(void *v, const void *data, size_t n)
{
	struct fixture *f = v;
	assert(n >= sizeof(f->request));
	memcpy(&f->request, data, sizeof(f->request));
	return f->mode == 4 ? 42 : 0;
}
static void poll_packet(void *v)
{
	struct fixture *f = v;
	struct { struct mi8_apr_header h; uint32_t words[2]; } p = {
		.h = { .field = 0x250, .size = sizeof(p) },
	};
	f->polls++;
	if (f->mode == 3) return;
	p.h.token = f->request.token;
	if (f->mode == 1) {
		p.h.opcode = MI8_APR_BASIC_RESULT;
		p.words[0] = f->request.opcode;
		p.words[1] = 0;
		mi8_apr_client_receive(f->client, &p, sizeof(p));
		assert(!f->client->done);
	}
	p.h.opcode = f->mode == 1 ? 0x1234 : MI8_APR_BASIC_RESULT;
	p.words[0] = f->mode == 1 ? 99 : f->request.opcode;
	p.words[1] = f->mode == 2 ? 5 : 0;
	/* Ignore an unrelated and a truncated packet before the matching one. */
	p.h.token++;
	mi8_apr_client_receive(f->client, &p, sizeof(p));
	assert(!f->client->done);
	p.h.token--;
	mi8_apr_client_receive(f->client, &p, sizeof(p) - 1);
	assert(!f->client->done);
	mi8_apr_client_receive(f->client, &p, sizeof(p));
}
static void sleep_time(void *v, uint32_t n) { ((struct fixture *)v)->now += n; }
static uint64_t now(void *v) { return ((struct fixture *)v)->now; }
int main(void)
{
	struct fixture f = { 0 };
	struct mi8_apr_client c = { .transport = &f, .send = send_packet, .poll = poll_packet,
	    .sleep_us = sleep_time, .now_us = now };
	f.client = &c;
	assert(mi8_apr_client_call(&c, 4, 0x1006, 0x4567, 0, NULL, 0, 3000) == 0);
	assert(c.dsp_status == 0 && c.response_opcode == MI8_APR_BASIC_RESULT);
	assert(f.request.from_service == 4 && f.request.to_port == 0x1006);
	f.mode = 1;
	assert(mi8_apr_client_call(&c, 3, 0, 0x5678, 0x1234, NULL, 0, 3000) == 0);
	assert(c.response_opcode == 0x1234 && c.response_length == 8);
	f.mode = 2;
	assert(mi8_apr_client_call(&c, 4, 0, 1, 0, NULL, 0, 3000) == 0 && c.dsp_status == 5);
	f.mode = 3;
	assert(mi8_apr_client_call(&c, 4, 0, 1, 0, NULL, 0, 3000) == -3 && !c.waiting_token);
	f.mode = 4;
	assert(mi8_apr_client_call(&c, 4, 0, 1, 0, NULL, 0, 3000) == 42 && !c.waiting_token);
	puts("APR commands: matching, DSP errors, transport failure and timeout passed");
}
