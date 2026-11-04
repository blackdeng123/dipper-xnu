/* SPDX-License-Identifier: APSL-2.0
 * ASM/ADM protocol and command ordering adapted from the project's existing
 * redwood_sound.c, https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_ASM_H
#define MI8_ASM_H
#include "mi8-apr-client.h"

#define MI8_ASM_OPEN 0x00010db3u
#define MI8_ASM_FORMAT 0x00010d98u
#define MI8_ASM_RUN 0x00010daau
#define MI8_ASM_CLOSE 0x00010bcdu
#define MI8_ADM_ROUTE 0x00010325u
#define MI8_ASM_STREAM_PORT 0x0101u

struct mi8_asm_result { int open, route, format, run, close; };

static inline int
mi8_asm_command(struct mi8_apr_client *client, uint8_t service, uint16_t port,
    uint32_t opcode, const void *payload, size_t length)
{
	int rc = mi8_apr_client_call(client, service, port, opcode, 0, payload, length, 1000000);
	if (rc) return rc;
	if (client->response_opcode != MI8_APR_BASIC_RESULT) return -4;
	return client->dsp_status ? (int)(1000u + client->dsp_status) : 0;
}

/* Start a session. The caller must close it even on an ambiguous error; the
 * DSP may have applied OPEN before a timeout. No samples are submitted here. */
static inline struct mi8_asm_result
mi8_asm_start(struct mi8_apr_client *client, uint16_t copp)
{
	struct { uint32_t flags; uint16_t sink, bits; uint32_t topology, format; }
	    __attribute__((packed)) open = { 0, 0, 16, 0x00010c68u, 0x00010da5u };
	struct { uint32_t matrix, sessions; uint16_t session, copps, copp[2]; }
	    __attribute__((packed)) route = { 0, 1, 1, 1, { copp, 0 } };
	struct { uint32_t size; uint16_t channels, bits; uint32_t rate; uint16_t signed_pcm, reserved; uint8_t map[8]; }
	    __attribute__((packed)) format = { 20, 2, 16, 48000, 1, 0, { 1, 2 } };
	uint32_t run[3] = { 0, 0, 0 };
	struct mi8_asm_result r = { -9, -9, -9, -9, -9 };
	r.open = mi8_asm_command(client, 7, MI8_ASM_STREAM_PORT, MI8_ASM_OPEN, &open, sizeof(open));
	if (!r.open) {
		r.route = mi8_asm_command(client, 8, 0, MI8_ADM_ROUTE, &route, sizeof(route));
		if (!r.route) {
			r.format = mi8_asm_command(client, 7, MI8_ASM_STREAM_PORT, MI8_ASM_FORMAT, &format, sizeof(format));
			if (!r.format) r.run = mi8_asm_command(client, 7, MI8_ASM_STREAM_PORT, MI8_ASM_RUN, run, sizeof(run));
		}
	}
	return r;
}

static inline int
mi8_asm_close(struct mi8_apr_client *client)
{
	return mi8_asm_command(client, 7, MI8_ASM_STREAM_PORT, MI8_ASM_CLOSE, NULL, 0);
}

/* Existing isolated probe: start and immediately unwind without data. */
static inline struct mi8_asm_result
mi8_asm_probe(struct mi8_apr_client *client, uint16_t copp)
{
	struct mi8_asm_result r = mi8_asm_start(client, copp);
	r.close = mi8_asm_close(client);
	return r;
}
#endif
