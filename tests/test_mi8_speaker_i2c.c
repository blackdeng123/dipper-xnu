/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
#define __unused __attribute__((unused))
#define LCK_GRP_DECLARE(name, description) int name
#define LCK_MTX_DECLARE(name, group) int name
#define NSEC_PER_USEC 1000
static uint32_t pins[2][2], clocks[0x17624 / 4];
static int held, maps, unsafe_high;
static void lck_mtx_lock(int *lock) { (void)lock; assert(!held); held = 1; }
static void lck_mtx_unlock(int *lock) { (void)lock; assert(held); held = 0; }
static vm_offset_t gcc(void) { return (vm_offset_t)clocks; }
static vm_offset_t ml_io_map(vm_offset_t physical, unsigned size)
{
	assert(size == 0x1000); maps++;
	if (physical == 0x03555000) return (vm_offset_t)pins[0];
	if (physical == 0x03556000) return (vm_offset_t)pins[1];
	return 0;
}
static void ml_io_unmap(vm_offset_t address, unsigned size) { assert(address && size == 0x1000); maps--; }
static uint32_t rd(vm_offset_t address) { return *(uint32_t *)address; }
static void wr(vm_offset_t address, uint32_t value)
{
	*(uint32_t *)address = value;
	for (unsigned i = 0; i < 2; i++)
		if ((pins[i][0] & 0x200) && (pins[i][1] & 2)) unsafe_high++;
}
static void settle(void) { }
static uint64_t now;
static void nanoseconds_to_absolutetime(uint64_t ns, uint64_t *ticks) { *ticks = ns; }
static uint64_t mach_absolute_time(void) { return ++now; }
#include "mi8-speaker-i2c.inc"

int main(void)
{
	struct mi8_speaker_i2c s = { 0 };
	assert(mi8_speaker_i2c_open(&s, 85, 86) != 0 && !held && !maps);
	clocks[0x17620 / 4] = 0x80000000u;
	pins[0][0] = 4;
	assert(mi8_speaker_i2c_open(&s, 85, 86) != 0 && !held && !maps);
	pins[0][0] = pins[1][0] = 1;
	pins[0][1] = pins[1][1] = 3; /* input high, original output latch high */
	assert(mi8_speaker_i2c_open(&s, 85, 86) == 0 && held && maps == 2);
	assert(s.bus.drive(s.bus.context, MI8_I2C_SDA, 1) == 0);
	assert(pins[0][0] == 0x200 && pins[0][1] == 0);
	assert(mi8_speaker_i2c_close(&s) == 0 && !held && !maps && !unsafe_high);
	assert(pins[0][0] == 1 && pins[0][1] == 2 && pins[1][0] == 1 && pins[1][1] == 2);
	assert(mi8_speaker_i2c_open(&s, 85, 86) == 0);
	pins[0][0] = 4; /* another owner changed pin function */
	assert(s.bus.drive(s.bus.context, MI8_I2C_SDA, 1) != 0);
	assert(mi8_speaker_i2c_close(&s) != 0 && pins[0][0] == 4 && !held && !maps);
	puts("speaker I2C GPIO: clock guard, open drain, restoration and ownership passed");
}
