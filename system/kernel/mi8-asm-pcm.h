/* SPDX-License-Identifier: APSL-2.0
 * ASM shared-memory and PCM write protocol adapted from redwood_sound.c:
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 * WRITE_DONE payload fields are documented at:
 * https://android.googlesource.com/kernel/msm/+/9eae482c1889b5847ff0ab1a2b3d984f60269764/include/sound/apr_audio-v2.h
 */
#ifndef MI8_ASM_PCM_H
#define MI8_ASM_PCM_H
#include "mi8-apr-client.h"

#define MI8_PCM_MAP 0x00010d92u
#define MI8_PCM_MAP_REPLY 0x00010d93u
#define MI8_PCM_UNMAP 0x00010d94u
#define MI8_PCM_WRITE 0x00010dabu
#define MI8_PCM_WRITE_DONE 0x00010d99u
#define MI8_PCM_UNDERFLOW 0x00010c17u
#define MI8_PCM_PERIODS 8u
#define MI8_PCM_STRIDE 4096u
#define MI8_PCM_MAX_BYTES 3840u
#define MI8_PCM_BYTES (MI8_PCM_PERIODS * MI8_PCM_STRIDE)
#define MI8_PCM_TOKEN_BIT 0x80000000u

/* All calls, including receive, are serialized by the owner's worker thread.
 * The iOS audio write callback must queue samples separately; send may sleep.
 * DMA memory, cache synchronization and the SMMU are the caller's responsibility.
 */
struct mi8_pcm {
	struct mi8_apr_client *client;
	uint8_t *memory;
	uint64_t dsp_address;
	void (*sync_for_device)(void *, void *, size_t);
	void *sync_context;
	uint32_t handle;
	uint32_t token[MI8_PCM_PERIODS];
	uint32_t inflight, completed, errors, underflows;
	int mapped, quarantined;
};

static inline int
mi8_pcm_init(struct mi8_pcm *p, struct mi8_apr_client *client, void *memory,
    size_t bytes, uint64_t dsp_address, void (*sync)(void *, void *, size_t), void *sync_context)
{
	if (!p || !client || !client->send || !memory || !sync || bytes < MI8_PCM_BYTES ||
	    (dsp_address & (MI8_PCM_STRIDE - 1)) ||
	    dsp_address > ((1ull << 36) - MI8_PCM_BYTES)) return -1;
	memset(p, 0, sizeof(*p));
	p->client = client; p->memory = memory; p->dsp_address = dsp_address;
	p->sync_for_device = sync; p->sync_context = sync_context;
	return 0;
}

/* A timed-out MAP or UNMAP is ambiguous: preserve the backing memory until the
 * DSP has been stopped. A zero DSP handle is allowed by this interface. */
static inline int
mi8_pcm_map(struct mi8_pcm *p)
{
	struct { uint16_t pool, regions; uint32_t property, low, high, size; }
	    __attribute__((packed)) data;
	int rc;
	if (!p || !p->client || p->mapped || p->quarantined) return -1;
	memset(&data, 0, sizeof(data));
	data.pool = 3; data.regions = 1; data.low = (uint32_t)p->dsp_address;
	data.high = (uint32_t)(p->dsp_address >> 32); data.size = MI8_PCM_BYTES;
	rc = mi8_apr_client_call(p->client, 7, 0, MI8_PCM_MAP, MI8_PCM_MAP_REPLY, &data, sizeof(data), 1000000);
	if (rc) { p->quarantined = 1; return rc; }
	if (p->client->response_opcode == MI8_APR_BASIC_RESULT)
		return p->client->dsp_status ? (int)(1000u + p->client->dsp_status) : -4;
	if (p->client->response_length < sizeof(p->handle)) { p->quarantined = 1; return -4; }
	memcpy(&p->handle, p->client->response, sizeof(p->handle));
	p->mapped = 1;
	return 0;
}

static inline int
mi8_pcm_unmap(struct mi8_pcm *p)
{
	int rc;
	/* The caller must first close the ASM session and drain its completions. */
	if (!p || !p->mapped || p->inflight || p->quarantined) return -1;
	rc = mi8_apr_client_call(p->client, 7, 0, MI8_PCM_UNMAP, 0, &p->handle, sizeof(p->handle), 1000000);
	if (rc) { p->quarantined = 1; return rc; }
	if (p->client->dsp_status) { p->quarantined = 1; return (int)(1000u + p->client->dsp_status); }
	p->mapped = 0;
	return 0;
}

/* The same slot cannot be reused until a matching WRITE_DONE is received. */
static inline int
mi8_pcm_submit(struct mi8_pcm *p, unsigned slot, const void *samples, uint32_t bytes)
{
	struct { struct mi8_apr_header h; uint32_t word[8]; } packet;
	uint32_t bit, token;
	uint64_t address;
	int rc;
	if (!p || !p->mapped || p->quarantined || !samples || slot >= MI8_PCM_PERIODS ||
	    !bytes || bytes > MI8_PCM_MAX_BYTES || (bytes & 3)) return -1;
	bit = 1u << slot;
	if (p->inflight & bit) return -2;
	/* Keep the sequence on the transport client so reinitializing a buffer
	 * cannot mistake a late completion for its first new submission. */
	do {
		unsigned i;
		if (++p->client->next_write_token >= MI8_PCM_TOKEN_BIT) p->client->next_write_token = 1;
		token = MI8_PCM_TOKEN_BIT | p->client->next_write_token;
		for (i = 0; i < MI8_PCM_PERIODS; i++)
			if ((p->inflight & (1u << i)) && p->token[i] == token) break;
		if (i == MI8_PCM_PERIODS) break;
	} while (1);
	address = p->dsp_address + (uint64_t)slot * MI8_PCM_STRIDE;
	memcpy(p->memory + slot * MI8_PCM_STRIDE, samples, bytes);
	p->sync_for_device(p->sync_context, p->memory + slot * MI8_PCM_STRIDE, bytes);
	memset(&packet, 0, sizeof(packet));
	packet.h.field = 0x250; packet.h.size = sizeof(packet);
	packet.h.from_service = packet.h.to_service = 7;
	packet.h.from_domain = 5; packet.h.to_domain = 4;
	packet.h.from_port = packet.h.to_port = 0x0101;
	packet.h.token = token; packet.h.opcode = MI8_PCM_WRITE;
	packet.word[0] = (uint32_t)address; packet.word[1] = (uint32_t)(address >> 32);
	packet.word[2] = p->handle; packet.word[3] = bytes; packet.word[4] = slot;
	p->token[slot] = token; p->inflight |= bit;
	rc = p->client->send(p->client->transport, &packet, sizeof(packet));
	/* On a send error the DSP may have received part or all of the packet. */
	if (rc) p->quarantined = 1;
	return rc;
}

/* Hook this to mi8_apr_client.event, on the same serialized worker thread. */
static inline void
mi8_pcm_receive(void *context, const void *packet, size_t length)
{
	struct mi8_pcm *p = context;
	struct mi8_apr_header h;
	const uint8_t *payload;
	size_t n;
	uint32_t words[4], slot;
	if (!p || !packet || length < sizeof(h)) return;
	memcpy(&h, packet, sizeof(h));
	if (h.from_service != 7) return;
	if (h.opcode == MI8_PCM_UNDERFLOW) {
		if (mi8_apr_response(packet, length, h.token, h.opcode, &payload, &n)) p->underflows++;
		return;
	}
	if (!(h.token & MI8_PCM_TOKEN_BIT)) return;
	for (slot = 0; slot < MI8_PCM_PERIODS; slot++)
		if ((p->inflight & (1u << slot)) && p->token[slot] == h.token) break;
	if (slot == MI8_PCM_PERIODS || !mi8_apr_response(packet, length, h.token, h.opcode, &payload, &n)) return;
	memset(words, 0, sizeof(words));
	if (h.opcode == MI8_PCM_WRITE_DONE) {
		uint64_t expected = p->dsp_address + (uint64_t)slot * MI8_PCM_STRIDE;
		if (n < sizeof(words)) return;
		memcpy(words, payload, sizeof(words));
		if (((uint64_t)words[1] << 32 | words[0]) != expected || words[2] != p->handle) return;
		if (words[3]) p->errors++;
	} else if (h.opcode == MI8_APR_BASIC_RESULT) {
		if (n < 2 * sizeof(uint32_t)) return;
		memcpy(words, payload, 2 * sizeof(uint32_t));
		if (words[0] != MI8_PCM_WRITE || words[1] == 0) return;
		p->errors++;
	} else return;
	p->inflight &= ~(1u << slot);
	p->completed++;
}
#endif
