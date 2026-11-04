/* SPDX-License-Identifier: APSL-2.0
 * Mi 8 experimental QUAT MI2S AFE port, with the amplifier kept off.
 * APR wire format is from the existing APSL-2.0 redwood_sound.c:
 * https://github.com/oosexgang/hakios-xnu-kernel
 * Protocol identifiers: https://android.googlesource.com/kernel/msm/+/afdfa6b819d48b1ce677be7973dd5dc4e1c7dee4/include/sound/apr_audio-v2.h
 */
#ifndef MI8_AFE_H
#define MI8_AFE_H
#include "mi8-apr-client.h"
#include "mi8-adm.h"

#define MI8_AFE_SERVICE 4u
#define MI8_AFE_PORT 0x1006u
#define MI8_AFE_CLOCK 0x106u
#define MI8_AFE_SERVICE_SET 0x000100f3u
#define MI8_AFE_PORT_SET 0x000100efu
#define MI8_AFE_PORT_START 0x000100e5u
#define MI8_AFE_PORT_STOP 0x000100e6u

struct mi8_afe_parameter { uint32_t module, parameter; uint16_t size, reserved; } __attribute__((packed));
struct mi8_afe_clock { uint32_t version, id, frequency; uint16_t attribute, root; uint32_t enable; } __attribute__((packed));
struct mi8_afe_i2s { uint32_t version; uint16_t bits, line, stereo, ws_source; uint32_t rate; uint16_t format, reserved; } __attribute__((packed));
struct mi8_afe_port_cmd { uint16_t port, reserved; } __attribute__((packed));

struct mi8_afe_result { int clock_on, configure, start, stop, clock_off; struct mi8_adm_result adm; };

static inline int
mi8_afe_command(struct mi8_apr_client *client, uint32_t opcode, const void *payload, size_t size)
{
	int rc = mi8_apr_client_call(client, MI8_AFE_SERVICE, 0, opcode, 0, payload, size, 1000000);
	if (rc) return rc;
	if (client->response_opcode != MI8_APR_BASIC_RESULT) return -4;
	return client->dsp_status ? (int)(1000u + client->dsp_status) : 0;
}

static inline int
mi8_afe_clock_set(struct mi8_apr_client *client, int enable)
{
	struct { uint32_t length, addr_lo, addr_hi, handle; struct mi8_afe_parameter p; struct mi8_afe_clock c; }
	    __attribute__((packed)) packet = { .length = sizeof(struct mi8_afe_parameter) + sizeof(struct mi8_afe_clock),
	    .p = { 0x1028f, 0x10290, sizeof(struct mi8_afe_clock), 0 },
	    .c = { 1, MI8_AFE_CLOCK, 3072000, 1, 0, enable != 0 } };
	return mi8_afe_command(client, MI8_AFE_SERVICE_SET, &packet, sizeof(packet));
}

static inline int
mi8_afe_configure(struct mi8_apr_client *client)
{
	struct { uint16_t port, length; uint32_t addr_lo, addr_hi, handle; struct mi8_afe_parameter p; struct mi8_afe_i2s i; }
	    __attribute__((packed)) packet = { .port = MI8_AFE_PORT,
	    .length = sizeof(struct mi8_afe_parameter) + sizeof(struct mi8_afe_i2s),
	    .p = { 0x1020c, 0x1020d, sizeof(struct mi8_afe_i2s), 0 },
	    .i = { 1, 24, 1, 1, 1, 48000, 0, 0 } };
	return mi8_afe_command(client, MI8_AFE_PORT_SET, &packet, sizeof(packet));
}

/* Start the physical port without configuring GPIO or enabling the amplifier.
 * Always call mi8_afe_close, even on failure: a timeout is ambiguous. */
static inline struct mi8_afe_result
mi8_afe_open(struct mi8_apr_client *client)
{
	struct mi8_afe_result r = { -9, -9, -9, -9, -9, { -9, -9, 0, { -9, -9, -9, -9, -9 } } };
	struct mi8_afe_port_cmd port = { MI8_AFE_PORT, 0 };
	r.clock_on = mi8_afe_clock_set(client, 1);
	if (!r.clock_on) {
		r.configure = mi8_afe_configure(client);
		if (!r.configure) {
			r.start = mi8_afe_command(client, MI8_AFE_PORT_START, &port, sizeof(port));
		}
	}
	return r;
}

static inline void
mi8_afe_close(struct mi8_apr_client *client, struct mi8_afe_result *r)
{
	struct mi8_afe_port_cmd port = { MI8_AFE_PORT, 0 };
	if (r->start != -9) r->stop = mi8_afe_command(client, MI8_AFE_PORT_STOP, &port, sizeof(port));
	r->clock_off = mi8_afe_clock_set(client, 0);
}

/* The isolated probe unwinds the ADM and AFE resources before returning. */
static inline struct mi8_afe_result
mi8_afe_probe(struct mi8_apr_client *client)
{
	struct mi8_afe_result r = mi8_afe_open(client);
	if (!r.start) r.adm = mi8_adm_probe(client, MI8_AFE_PORT);
	mi8_afe_close(client, &r);
	return r;
}
#endif
