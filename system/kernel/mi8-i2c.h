/* SPDX-License-Identifier: APSL-2.0
 * Open-drain I2C transaction layer derived from the project's Mi 8 FTS
 * bit-banged bus (mi8_fts.c in patches/xnu-mi8.patch), copyright 2026
 * yanei and dipper-xnu contributors, MIT; see LICENSES/MI8-packaging-MIT.txt.
 * Protocol reference:
 * https://www.nxp.com/docs/en/user-guide/UM10204.pdf
 * The platform owns pin muxing, power, bus serialization, and timing.
 */
#ifndef MI8_I2C_H
#define MI8_I2C_H
#include <stddef.h>
#include <stdint.h>

enum { MI8_I2C_SDA, MI8_I2C_SCL };
enum { MI8_I2C_OK, MI8_I2C_INVALID = -1, MI8_I2C_TIMEOUT = -2,
       MI8_I2C_NACK = -3, MI8_I2C_BUSY = -4, MI8_I2C_IO = -5 };

struct mi8_i2c_bus {
	void *context;
	/* low=1 drives low, low=0 releases the line; never actively drive high. */
	int (*drive)(void *, unsigned line, int low);
	int (*level)(void *, unsigned line); /* 0/1, negative on failure */
	void (*delay_us)(void *, uint32_t);
	uint32_t half_period_us, stretch_limit_us;
};

static inline int
mi8_i2c_line(struct mi8_i2c_bus *b, unsigned line, int low)
{
	return b->drive(b->context, line, low) ? MI8_I2C_IO : MI8_I2C_OK;
}

static inline int
mi8_i2c_clock_high(struct mi8_i2c_bus *b)
{
	int v, rc = mi8_i2c_line(b, MI8_I2C_SCL, 0);
	if (rc) return rc;
	for (uint32_t elapsed = 0; ; elapsed++) {
		v = b->level(b->context, MI8_I2C_SCL);
		if (v < 0) return MI8_I2C_IO;
		if (v) return MI8_I2C_OK;
		if (elapsed >= b->stretch_limit_us) return MI8_I2C_TIMEOUT;
		b->delay_us(b->context, 1);
	}
}

static inline int
mi8_i2c_start(struct mi8_i2c_bus *b)
{
	int rc, sda;
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 0)) || (rc = mi8_i2c_clock_high(b))) return rc;
	b->delay_us(b->context, b->half_period_us);
	sda = b->level(b->context, MI8_I2C_SDA);
	if (sda < 0) return MI8_I2C_IO;
	if (!sda) return MI8_I2C_BUSY;
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 1))) return rc;
	b->delay_us(b->context, b->half_period_us);
	return mi8_i2c_line(b, MI8_I2C_SCL, 1);
}

static inline int
mi8_i2c_stop(struct mi8_i2c_bus *b)
{
	int rc;
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 1))) return rc;
	b->delay_us(b->context, b->half_period_us);
	if ((rc = mi8_i2c_clock_high(b))) return rc;
	b->delay_us(b->context, b->half_period_us);
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 0))) return rc;
	b->delay_us(b->context, b->half_period_us);
	return MI8_I2C_OK;
}

static inline int
mi8_i2c_write_byte(struct mi8_i2c_bus *b, uint8_t byte)
{
	int rc, ack;
	for (int bit = 7; bit >= 0; bit--) {
		if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, !(byte & (1u << bit))))) return rc;
		b->delay_us(b->context, b->half_period_us);
		if ((rc = mi8_i2c_clock_high(b))) return rc;
		b->delay_us(b->context, b->half_period_us);
		if ((rc = mi8_i2c_line(b, MI8_I2C_SCL, 1))) return rc;
	}
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 0))) return rc;
	b->delay_us(b->context, b->half_period_us);
	if ((rc = mi8_i2c_clock_high(b))) return rc;
	b->delay_us(b->context, b->half_period_us);
	ack = b->level(b->context, MI8_I2C_SDA);
	rc = mi8_i2c_line(b, MI8_I2C_SCL, 1);
	if (rc) return rc;
	return ack < 0 ? MI8_I2C_IO : ack ? MI8_I2C_NACK : MI8_I2C_OK;
}

static inline int
mi8_i2c_read_byte(struct mi8_i2c_bus *b, uint8_t *out, int ack)
{
	uint8_t value = 0;
	int rc, level;
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 0))) return rc;
	for (int bit = 0; bit < 8; bit++) {
		b->delay_us(b->context, b->half_period_us);
		if ((rc = mi8_i2c_clock_high(b))) return rc;
		b->delay_us(b->context, b->half_period_us);
		level = b->level(b->context, MI8_I2C_SDA);
		if (level < 0) return MI8_I2C_IO;
		value = (uint8_t)(value << 1 | !!level);
		if ((rc = mi8_i2c_line(b, MI8_I2C_SCL, 1))) return rc;
	}
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, ack))) return rc;
	b->delay_us(b->context, b->half_period_us);
	if ((rc = mi8_i2c_clock_high(b))) return rc;
	b->delay_us(b->context, b->half_period_us);
	if ((rc = mi8_i2c_line(b, MI8_I2C_SCL, 1))) return rc;
	if ((rc = mi8_i2c_line(b, MI8_I2C_SDA, 0))) return rc;
	*out = value;
	return MI8_I2C_OK;
}

/* Combined write/read with a repeated START; caller serializes the bus. */
static inline int
mi8_i2c_transfer(struct mi8_i2c_bus *b, uint8_t address,
    const uint8_t *write_data, size_t write_size, uint8_t *read_data, size_t read_size)
{
	int rc, stop;
	if (!b || !b->drive || !b->level || !b->delay_us || address > 0x7f ||
	    !b->half_period_us || b->half_period_us > 1000 || !b->stretch_limit_us ||
	    b->stretch_limit_us > 100000 || (!write_size && !read_size) ||
	    (write_size && !write_data) || (read_size && !read_data)) return MI8_I2C_INVALID;
	rc = mi8_i2c_start(b);
	if (rc) goto release;
	if (write_size) {
		rc = mi8_i2c_write_byte(b, (uint8_t)(address << 1));
		for (size_t i = 0; !rc && i < write_size; i++) rc = mi8_i2c_write_byte(b, write_data[i]);
	}
	if (!rc && read_size) {
		if (write_size) rc = mi8_i2c_start(b);
		if (!rc) rc = mi8_i2c_write_byte(b, (uint8_t)(address << 1 | 1));
		for (size_t i = 0; !rc && i < read_size; i++) rc = mi8_i2c_read_byte(b, &read_data[i], i + 1 < read_size);
	}
	stop = mi8_i2c_stop(b);
	if (!rc) rc = stop;
release:
	/* On errors do not hold either wire low, even when STOP itself failed. */
	if (rc) { (void)mi8_i2c_line(b, MI8_I2C_SDA, 0); (void)mi8_i2c_line(b, MI8_I2C_SCL, 0); }
	return rc;
}
#endif
