/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-apr-response.h"

int main(void)
{
	struct { struct mi8_apr_header h; uint32_t status; } p = {
		.h = { .field = 0x250, .size = sizeof(p), .token = 7, .opcode = 0x110e8 },
		.status = 0,
	};
	const uint8_t *payload = NULL;
	size_t size = 0;
	assert(mi8_apr_response(&p, sizeof(p), 7, 0x110e8, &payload, &size));
	assert(size == 4 && payload == (const uint8_t *)&p.status);
	assert(!mi8_apr_response(&p, sizeof(p) - 1, 7, 0x110e8, &payload, &size));
	assert(!mi8_apr_response(&p, sizeof(p), 8, 0x110e8, &payload, &size));
	assert(!mi8_apr_response(&p, sizeof(p), 7, 1, &payload, &size));
	p.h.field = 0x240;
	assert(!mi8_apr_response(&p, sizeof(p), 7, 0x110e8, &payload, &size));
	p.h.field = 0x250; p.h.size = 19;
	assert(!mi8_apr_response(&p, sizeof(p), 7, 0x110e8, &payload, &size));
	puts("APR response matching and malformed-packet checks passed");
}
