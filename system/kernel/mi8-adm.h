/* SPDX-License-Identifier: APSL-2.0
 * ADM path probe, based on the APR interface used in the existing
 * redwood_sound.c, https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_ADM_H
#define MI8_ADM_H
#include "mi8-apr-client.h"
#include "mi8-asm.h"
#define MI8_ADM_OPEN 0x00010326u
#define MI8_ADM_OPEN_REPLY 0x00010329u
#define MI8_ADM_CLOSE 0x00010327u

struct mi8_adm_result { int open, close; uint16_t copp; struct mi8_asm_result stream; };

/* The caller has started the AFE port and must close a successful open. */
static inline int
mi8_adm_open(struct mi8_apr_client *client, uint16_t port, uint16_t *copp)
{
	struct { uint16_t flags, mode, endpoint, endpoint2; uint32_t topology;
	    uint16_t channels, bits; uint32_t rate; uint8_t channel_map[8]; } __attribute__((packed)) open = {
	    0, 1, port, 0xffff, 0x00010312u, 2, 24, 48000, { 1, 2 } };
	uint32_t words[2];
	int rc;
	if (!copp) return -1;
	rc = mi8_apr_client_call(client, 8, port, MI8_ADM_OPEN, MI8_ADM_OPEN_REPLY, &open, sizeof(open), 1000000);
	if (rc) return rc;
	if (client->response_opcode == MI8_APR_BASIC_RESULT) {
		return client->dsp_status ? (int)(1000u + client->dsp_status) : -4;
	}
	if (client->response_length < sizeof(words)) return -5;
	memcpy(words, client->response, sizeof(words));
	if (words[0]) return (int)(1000u + words[0]);
	*copp = (uint16_t)words[1];
	return 0;
}

static inline int
mi8_adm_close(struct mi8_apr_client *client, uint16_t copp)
{
	int rc = mi8_apr_client_call(client, 8, copp, MI8_ADM_CLOSE, 0, NULL, 0, 1000000);
	return !rc && client->dsp_status ? (int)(1000u + client->dsp_status) : rc;
}

/* Existing isolated test closes the stream and device path before returning. */
static inline struct mi8_adm_result
mi8_adm_probe(struct mi8_apr_client *client, uint16_t port)
{
	struct mi8_adm_result r = { -9, -9, 0, { -9, -9, -9, -9, -9 } };
	r.open = mi8_adm_open(client, port, &r.copp);
	if (r.open) return r;
	r.stream = mi8_asm_probe(client, r.copp);
	r.close = mi8_adm_close(client, r.copp);
	return r;
}
#endif
