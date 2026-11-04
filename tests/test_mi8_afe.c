/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-afe.h"
struct fixture { struct mi8_apr_client *client; uint32_t commands[16]; unsigned count, reject_at, timeout_at; uint64_t now; };
static int send_command(void *v, const void *data, size_t size)
{
	struct fixture *f = v;
	struct mi8_apr_header h;
	struct { struct mi8_apr_header h; uint32_t cmd, status; } response = { 0 };
	memcpy(&h, data, sizeof(h));
	assert(size == h.size && (h.to_service == MI8_AFE_SERVICE || h.to_service == 7 || h.to_service == 8));
	assert(f->count < 16);
	f->commands[f->count++] = h.opcode;
	if (f->count == f->timeout_at) return 0;
	if (h.opcode == MI8_AFE_SERVICE_SET) {
		struct mi8_afe_clock clock;
		assert(size == sizeof(h) + 48);
		memcpy(&clock, (const uint8_t *)data + sizeof(h) + 28, sizeof(clock));
		assert(clock.id == 0x106 && clock.frequency == 3072000 && clock.attribute == 1);
	}
	if (h.opcode == MI8_AFE_PORT_SET) {
		const uint8_t *p = (const uint8_t *)data + sizeof(h);
		uint16_t port; struct mi8_afe_i2s i2s;
		memcpy(&port, p, sizeof(port));
		assert(port == 0x1006 && size == sizeof(h) + 48);
		memcpy(&i2s, p + 28, sizeof(i2s));
		assert(i2s.version == 1 && i2s.bits == 24 && i2s.line == 1 && i2s.stereo == 1 &&
		    i2s.ws_source == 1 && i2s.rate == 48000 && i2s.format == 0);
	}
	response.h.field = 0x250; response.h.size = sizeof(response);
	response.h.token = h.token; response.h.opcode = MI8_APR_BASIC_RESULT;
	response.cmd = h.opcode; response.status = f->count == f->reject_at ? 2 : 0;
	if (h.opcode == MI8_ADM_OPEN && !response.status) {
		assert(h.to_port == 0x1006);
		response.h.opcode = MI8_ADM_OPEN_REPLY;
		response.cmd = 0;
		response.status = 42;
	}
	if (h.opcode == MI8_ADM_CLOSE) assert(h.to_port == 42);
	mi8_apr_client_receive(f->client, &response, sizeof(response));
	return 0;
}
static void poll(void *v) { (void)v; }
static void sleep_us(void *v, uint32_t n) { ((struct fixture *)v)->now += n; }
static uint64_t now_us(void *v) { return ((struct fixture *)v)->now; }
int main(void)
{
	struct fixture f = { 0 };
	struct mi8_apr_client c = { .transport = &f, .send = send_command, .poll = poll,
	    .sleep_us = sleep_us, .now_us = now_us };
	struct mi8_afe_result r;
	f.client = &c;
	r = mi8_afe_probe(&c);
	assert(!r.clock_on && !r.configure && !r.start && !r.stop && !r.clock_off && !r.adm.open && !r.adm.close && r.adm.copp == 42);
	assert(!r.adm.stream.open && !r.adm.stream.route && !r.adm.stream.format && !r.adm.stream.run && !r.adm.stream.close);
	assert(f.count == 12 && f.commands[0] == MI8_AFE_SERVICE_SET && f.commands[1] == MI8_AFE_PORT_SET &&
	    f.commands[2] == MI8_AFE_PORT_START && f.commands[3] == MI8_ADM_OPEN && f.commands[4] == MI8_ASM_OPEN &&
	    f.commands[5] == MI8_ADM_ROUTE && f.commands[6] == MI8_ASM_FORMAT && f.commands[7] == MI8_ASM_RUN &&
	    f.commands[8] == MI8_ASM_CLOSE && f.commands[9] == MI8_ADM_CLOSE && f.commands[10] == MI8_AFE_PORT_STOP &&
	    f.commands[11] == MI8_AFE_SERVICE_SET);
	f.count = 0; f.reject_at = 2;
	r = mi8_afe_probe(&c);
	assert(!r.clock_on && r.configure == 1002 && r.start == -9 && r.stop == -9 && !r.clock_off);
	assert(f.count == 3 && f.commands[2] == MI8_AFE_SERVICE_SET);
	f.count = 0; f.reject_at = 3;
	r = mi8_afe_probe(&c);
	assert(r.start == 1002 && !r.stop && !r.clock_off && f.count == 5);
	f.count = 0; f.reject_at = 0; f.timeout_at = 3;
	r = mi8_afe_probe(&c);
	assert(r.start == -3 && !r.stop && !r.clock_off && f.count == 5);
	f.count = 0; f.timeout_at = 0; f.reject_at = 4;
	r = mi8_afe_probe(&c);
	assert(r.adm.open == 1002 && r.adm.close == -9 && !r.stop && !r.clock_off && f.count == 6);
	f.count = 0; f.reject_at = 6;
	r = mi8_afe_probe(&c);
	assert(r.adm.stream.route == 1002 && r.adm.stream.run == -9 && !r.adm.stream.close &&
	    !r.adm.close && !r.stop && !r.clock_off && f.count == 10);
	puts("Mi 8 AFE port sequencing and failure cleanup passed");
}
