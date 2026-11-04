/* SPDX-License-Identifier: MIT
 * APR response validation for the Mi 8 audio transport.
 * Packet layout: https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 */
#ifndef MI8_APR_RESPONSE_H
#define MI8_APR_RESPONSE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct mi8_apr_header {
	uint16_t field, size;
	uint8_t from_service, from_domain;
	uint16_t from_port;
	uint8_t to_service, to_domain;
	uint16_t to_port;
	uint32_t token, opcode;
};

/* Returns 1 for a matching well-formed response; payload points into packet. */
static inline int
mi8_apr_response(const void *packet, size_t length, uint32_t token, uint32_t opcode,
    const uint8_t **payload, size_t *payload_length)
{
	struct mi8_apr_header h;
	size_t header_length;
	if (!packet || !payload || !payload_length || length < sizeof(h)) return 0;
	memcpy(&h, packet, sizeof(h));
	header_length = ((h.field >> 4) & 15u) * 4u;
	if (header_length < sizeof(h) || header_length > h.size || h.size > length ||
	    h.token != token || h.opcode != opcode) return 0;
	*payload = (const uint8_t *)packet + header_length;
	*payload_length = h.size - header_length;
	return 1;
}
#endif
