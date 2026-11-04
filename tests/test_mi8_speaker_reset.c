/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uintptr_t vm_offset_t;
#define __unused __attribute__((unused))
#define LCK_GRP_DECLARE(name, description) int name
#define LCK_MTX_DECLARE(name, group) int name
#define NSEC_PER_MSEC 1000000
static uint32_t pin[2];
static int held, mapped, stuck_high;
static unsigned waited_ms;
static void lck_mtx_lock(int *lock) { (void)lock; assert(!held); held = 1; }
static void lck_mtx_unlock(int *lock) { (void)lock; assert(held); held = 0; }
static vm_offset_t ml_io_map(vm_offset_t physical, unsigned size)
{
	assert(physical == 0x0354c000 && size == 0x1000); mapped++; return (vm_offset_t)pin;
}
static void ml_io_unmap(vm_offset_t address, unsigned size) { assert(address == (vm_offset_t)pin && size == 0x1000); mapped--; }
static uint32_t rd(vm_offset_t address)
{
	if (address == (vm_offset_t)(pin + 1) && (pin[0] & 0x200))
		return stuck_high ? 3 : (pin[1] & 2) | ((pin[1] >> 1) & 1);
	return *(uint32_t *)address;
}
static void wr(vm_offset_t address, uint32_t value) { *(uint32_t *)address = value; }
static void settle(void) { }
static void delay_for_interval(uint32_t ms, int unit) { assert(unit == NSEC_PER_MSEC); waited_ms += ms; }
#include "mi8-speaker-reset.inc"

int main(void)
{
	struct mi8_speaker_reset s = { 0 };
	pin[0] = 4; pin[1] = 2;
	assert(mi8_speaker_reset_open(&s) == -1 && !held && !mapped && pin[0] == 4);
	pin[0] = 1;
	assert(mi8_speaker_reset_open(&s) == 0 && held && mapped && s.asserted && waited_ms == 5);
	assert(pin[0] == 0x200 && pin[1] == 0);
	assert(mi8_speaker_reset_release(&s) == 0 && !s.asserted && waited_ms == 7);
	assert(pin[1] == 2);
	pin[0] = 4; /* changed by another owner: never clobber it */
	assert(mi8_speaker_reset_close(&s) == -1 && pin[0] == 4 && held && mapped);
	pin[0] = 0x200;
	assert(mi8_speaker_reset_close(&s) == 0 && !held && !mapped && waited_ms == 12);
	assert(pin[0] == 1 && pin[1] == 2);
	stuck_high = 1;
	assert(mi8_speaker_reset_open(&s) == -2 && s.uncertain && held && mapped);
	assert(mi8_speaker_reset_release(&s) == -1);
	stuck_high = 0;
	assert(mi8_speaker_reset_close(&s) == 0 && !held && !mapped && !s.uncertain);
	puts("speaker reset: input ownership, pulse timing, mismatch and restore passed");
}
