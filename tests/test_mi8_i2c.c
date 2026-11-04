/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mi8-i2c.h"

struct fake {
	int sda[32], count, next, low[2], stuck_clock;
	unsigned clocks, delays;
};
static int drive(void *ctx, unsigned line, int low)
{
	struct fake *f = ctx;
	assert(line <= MI8_I2C_SCL && (low == 0 || low == 1));
	if (line == MI8_I2C_SCL && !low && f->low[line]) f->clocks++;
	f->low[line] = low;
	return 0;
}
static int level(void *ctx, unsigned line)
{
	struct fake *f = ctx;
	if (line == MI8_I2C_SCL) return !f->low[line] && !f->stuck_clock;
	assert(!f->low[line]);
	assert(f->next < f->count);
	return f->sda[f->next++];
}
static void wait_us(void *ctx, uint32_t n)
{
	struct fake *f = ctx;
	assert(n > 0); f->delays += n;
}
static struct mi8_i2c_bus bus(struct fake *f)
{
	struct mi8_i2c_bus b = { f, drive, level, wait_us, 5, 10 };
	return b;
}
int main(void)
{
	/* start, address ACK, register ACK, repeated start, address ACK,
	 * then one byte read; the final byte is NACKed by the master. */
	struct fake f = { .sda = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1}, .count = 13 };
	struct mi8_i2c_bus b = bus(&f);
	uint8_t reg = 3, value = 0;
	assert(mi8_i2c_transfer(&b, 0x4c, &reg, 1, &value, 1) == 0);
	assert(value == 0xa5 && f.next == f.count && !f.low[0] && !f.low[1]);
	memset(&f, 0, sizeof(f)); f.sda[0] = 1; f.sda[1] = 1; f.count = 2;
	assert(mi8_i2c_transfer(&b, 0x4c, &reg, 1, NULL, 0) == MI8_I2C_NACK);
	assert(!f.low[0] && !f.low[1]);
	memset(&f, 0, sizeof(f)); f.stuck_clock = 1;
	assert(mi8_i2c_transfer(&b, 0x4c, &reg, 1, NULL, 0) == MI8_I2C_TIMEOUT);
	assert(!f.low[0] && !f.low[1] && f.delays == 10);
	memset(&f, 0, sizeof(f)); f.sda[0] = 0; f.count = 1;
	assert(mi8_i2c_transfer(&b, 0x4c, &reg, 1, NULL, 0) == MI8_I2C_BUSY);
	assert(!f.low[0] && !f.low[1]);
	puts("I2C: repeated-start read, NACK, clock timeout and busy bus passed");
}
