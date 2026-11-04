/* SPDX-License-Identifier: MIT
 * Serialized APR command client for the Mi 8 GLINK transport.
 * APR wire-format reference: https://github.com/oosexgang/hakios-xnu-kernel
 * (upstream driver: APSL-2.0). The transport must be polled by this caller.
 */
#ifndef MI8_APR_CLIENT_H
#define MI8_APR_CLIENT_H
#include "mi8-apr-response.h"

#define MI8_APR_BASIC_RESULT 0x000110e8u
#define MI8_APR_MAX_PAYLOAD 256u

struct mi8_apr_client {
	void *transport;
	int (*send)(void *, const void *, size_t);
	void (*poll)(void *);
	void (*sleep_us)(void *, uint32_t);
	uint64_t (*now_us)(void *);
	/* Unsolicited stream events are delivered on the transport polling thread. */
	void (*event)(void *, const void *, size_t);
	void *event_context;
	uint32_t next_token, next_write_token, waiting_token, waiting_opcode, wanted_opcode;
	uint32_t response_opcode, dsp_status;
	uint8_t response[MI8_APR_MAX_PAYLOAD];
	size_t response_length;
	int done;
};

/* Called synchronously by the transport poll/send callback. */
static inline void
mi8_apr_client_receive(struct mi8_apr_client *c, const void *packet, size_t length)
{
	const uint8_t *payload;
	size_t n;
	uint32_t words[2];
	if (!c) return;
	if (c->event) c->event(c->event_context, packet, length);
	if (!c->waiting_token || c->done) return;
	if (mi8_apr_response(packet, length, c->waiting_token, MI8_APR_BASIC_RESULT, &payload, &n)) {
		if (n < sizeof(words)) return;
		memcpy(words, payload, sizeof(words));
		if (words[0] != c->waiting_opcode) return;
		/* Some commands acknowledge first and send their data afterwards. */
		if (words[1] == 0 && c->wanted_opcode) return;
		c->dsp_status = words[1];
		c->response_opcode = MI8_APR_BASIC_RESULT;
	} else if (c->wanted_opcode && mi8_apr_response(packet, length, c->waiting_token,
	    c->wanted_opcode, &payload, &n)) {
		c->dsp_status = 0;
		c->response_opcode = c->wanted_opcode;
	} else return;
	if (n > sizeof(c->response)) return;
	memcpy(c->response, payload, n);
	c->response_length = n;
	c->done = 1;
}

/* Caller serializes calls. A zero return means a matching response arrived;
 * a DSP error is separately reported in dsp_status. Negative values are local
 * failures, positive values are transport errors. */
static inline int
mi8_apr_client_call(struct mi8_apr_client *c, uint8_t service, uint16_t port,
    uint32_t opcode, uint32_t wanted, const void *payload, size_t length,
    uint32_t timeout_us)
{
	uint8_t packet[sizeof(struct mi8_apr_header) + MI8_APR_MAX_PAYLOAD];
	struct mi8_apr_header h = { 0 };
	uint64_t started;
	int error;
	if (!c || !c->send || !c->poll || !c->sleep_us || !c->now_us ||
	    (length && !payload) || length > MI8_APR_MAX_PAYLOAD || !timeout_us) return -1;
	if (c->waiting_token) return -2;
	if (++c->next_token == 0 || c->next_token >= 0x01000000u) c->next_token = 1;
	h.field = 0x250; h.size = (uint16_t)(sizeof(h) + length);
	h.from_service = h.to_service = service;
	h.from_domain = 5; h.to_domain = 4;
	h.from_port = h.to_port = port;
	h.token = c->next_token; h.opcode = opcode;
	memcpy(packet, &h, sizeof(h));
	if (length) memcpy(packet + sizeof(h), payload, length);
	c->waiting_token = h.token; c->waiting_opcode = opcode; c->wanted_opcode = wanted;
	c->done = 0; c->response_length = 0; c->dsp_status = 0;
	started = c->now_us(c->transport);
	error = c->send(c->transport, packet, sizeof(h) + length);
	if (!error) {
		while (!c->done && c->now_us(c->transport) - started < timeout_us) {
			c->poll(c->transport);
			if (!c->done) c->sleep_us(c->transport, 1000);
		}
		if (!c->done) error = -3;
	}
	c->waiting_token = 0;
	return error;
}
#endif
