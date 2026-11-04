/* SPDX-License-Identifier: MIT
 * Execute a device-local TAS2557 initialization transcript without explicit
 * power or mute writes; the required software reset restores register defaults.
 * Register facts and reset timing:
 * https://www.ti.com/lit/ds/symlink/tas2557.pdf
 * This is not a playback or protection driver and is not enabled at boot.
 */
#ifndef MI8_TAS_INIT_H
#define MI8_TAS_INIT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MI8_TAS_MAX_RECORDS 1024u
#define MI8_TAS_MAX_WRITE 128u
#define MI8_TAS_MAX_READ 120u

struct mi8_tas_init_io {
	void *context;
	int (*transfer)(void *, uint8_t, const uint8_t *, size_t, uint8_t *, size_t);
	void (*delay_us)(void *, uint32_t);
};
static inline uint16_t mi8_tas_u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static inline uint32_t mi8_tas_u32(const uint8_t *p) { return (uint32_t)mi8_tas_u16(p) | (uint32_t)mi8_tas_u16(p + 2) << 16; }

/* The stream is checked in full before any bus write. Format: 8-byte magic,
 * u32 count, then count records of {u32 delay_us,u16 write_len,u16 read_len,
 * write bytes, expected read bytes}. Reads require a single register pointer.
 * The first three writes select book/page zero and perform software reset.
 * The caller must hold hardware reset low then release it before this call and
 * authenticate that the private transcript came from this exact device. */
static inline int
mi8_tas_init_validate(const uint8_t *p, size_t length)
{
	static const uint8_t magic[8] = { 'M','I','8','T','A','S','1',0 };
	uint32_t count, book = 0, page = 0;
	size_t at = 12;
	int reset_wait = 0;
	if (!p || length < at || memcmp(p, magic, 8)) return -1;
	count = mi8_tas_u32(p + 8);
	if (count < 3 || count > MI8_TAS_MAX_RECORDS) return -1;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t delay;
		uint16_t w, r;
		const uint8_t *data;
		if (length - at < 8) return -1;
		delay = mi8_tas_u32(p + at); w = mi8_tas_u16(p + at + 4); r = mi8_tas_u16(p + at + 6);
		at += 8;
		if (delay > 100000 || !w || w > MI8_TAS_MAX_WRITE || r > MI8_TAS_MAX_READ ||
		    (r && w != 1) || (size_t)w + r > length - at || (reset_wait && delay < 100)) return -1;
		reset_wait = 0; data = p + at;
		if (i == 0 && (w != 2 || r || data[0] != 0 || data[1] != 0)) return -1;
		if (i == 1 && (w != 2 || r || data[0] != 127 || data[1] != 0)) return -1;
		if (i == 2 && (w != 2 || r || data[0] != 1 || data[1] != 1)) return -1;
		if (r) {
			if ((uint32_t)data[0] + r > 128u) return -1;
			/* These sticky fault registers clear on read; don't silently
			 * consume protection evidence during a transcript check. */
			if (book == 0 && page == 0 &&
			    ((data[0] <= 0x68 && (uint32_t)data[0] + r > 0x68) ||
			     (data[0] <= 0x6c && (uint32_t)data[0] + r > 0x6c))) return -1;
		} else {
			uint32_t reg = data[0], last;
			if (w < 2) return -1;
			last = reg + w - 2;
			if (last > 127) return -1;
			if (reg == 0) { if (w != 2) return -1; page = data[1]; }
			else if (page == 0 && reg == 127) { if (w != 2) return -1; book = data[1]; }
			else {
				if (page == 0 && last == 127) return -1;
				if (book == 100 && page == 0 && reg <= 7 && last >= 7) return -1; /* soft mute */
				if (book == 0 && page == 0 &&
				    ((reg <= 4 && last >= 4) || (reg <= 5 && last >= 5) ||
				     (reg <= 7 && last >= 7))) return -1;
				if (book == 0 && page == 0 && reg <= 1 && last >= 1 && (data[1 - reg + 1] & 1)) {
					book = page = 0; reset_wait = 1;
				}
			}
		}
		at += (size_t)w + r;
	}
	return at == length && !reset_wait ? 0 : -1;
}

static inline int
mi8_tas_init_run(const uint8_t *p, size_t length, const struct mi8_tas_init_io *io, uint32_t *failed_record)
{
	uint8_t readback[MI8_TAS_MAX_READ];
	size_t at = 12;
	uint32_t count;
	if (failed_record) *failed_record = UINT32_MAX;
	if (!io || !io->transfer || !io->delay_us || mi8_tas_init_validate(p, length)) return -1;
	count = mi8_tas_u32(p + 8);
	for (uint32_t i = 0; i < count; i++) {
		uint32_t delay = mi8_tas_u32(p + at);
		uint16_t w = mi8_tas_u16(p + at + 4), r = mi8_tas_u16(p + at + 6);
		const uint8_t *data = p + at + 8;
		if (delay) io->delay_us(io->context, delay);
		if (io->transfer(io->context, 0x4c, data, w, readback, r) ||
		    (r && memcmp(readback, data + w, r))) {
			if (failed_record) *failed_record = i;
			return -2;
		}
		at += 8 + (size_t)w + r;
	}
	return 0;
}
#endif
