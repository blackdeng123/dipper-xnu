/* SPDX-License-Identifier: APSL-2.0
 * Mi 8 speaker control bus adapter, using the project's mi8_fts.c GPIO access
 * pattern (original work copyright 2026 yanei and dipper-xnu contributors,
 * MIT; see LICENSES/MI8-packaging-MIT.txt) and the existing I2C transaction
 * layer. This code has no automatic startup path.
 *
 * The caller supplies the SDA/SCL order after verifying board wiring, and
 * serializes all other owners of the pins and serial engine before open.
 */
#include "mi8-i2c.h"

#define MI8_SPK_TLMM_BASE 0x03500000ul
#define MI8_SPK_I2C_ENGINE_CBCR 0x17620u
#define MI8_SPK_CBCR_OFF (1u << 31)
#define MI8_SPK_PIN_OUTPUT (1u << 9)
#define MI8_SPK_PIN_FUNCTION (15u << 2)
#define MI8_SPK_PIN_CONFIG_LOW 0x200u

static LCK_GRP_DECLARE(mi8_spk_i2c_group, "mi8-speaker-i2c");
static LCK_MTX_DECLARE(mi8_spk_i2c_lock, &mi8_spk_i2c_group);

struct mi8_speaker_i2c {
	vm_offset_t pin[2];
	uint32_t saved_config[2], saved_output[2], expected_config[2];
	struct mi8_i2c_bus bus;
	int acquired;
};

static int
mi8_speaker_i2c_drive(void *context, unsigned line, int low)
{
	struct mi8_speaker_i2c *s = context;
	uint32_t next;
	if (!s || !s->acquired || line > MI8_I2C_SCL || (low != 0 && low != 1) ||
	    rd(s->pin[line]) != s->expected_config[line]) return -1;
	/* Preserve unrelated pin bits; input with no pull releases the line. */
	next = (s->saved_config[line] & ~0x3ffu) | (low ? MI8_SPK_PIN_CONFIG_LOW : 0);
	if (low) wr(s->pin[line] + 4, 0); /* set output data low before enabling */
	wr(s->pin[line], next);
	settle();
	if (rd(s->pin[line]) != next) return -1;
	s->expected_config[line] = next;
	return 0;
}

static int
mi8_speaker_i2c_level(void *context, unsigned line)
{
	struct mi8_speaker_i2c *s = context;
	if (!s || !s->acquired || line > MI8_I2C_SCL ||
	    rd(s->pin[line]) != s->expected_config[line]) return -1;
	return (int)(rd(s->pin[line] + 4) & 1u);
}

static void
mi8_speaker_i2c_wait(__unused void *context, uint32_t us)
{
	uint64_t ticks, deadline;
	/* Short sleeps on this platform can take milliseconds. Bit transitions
	 * must instead spin for a bounded time, as in the Mi 8 FTS driver. */
	nanoseconds_to_absolutetime((uint64_t)us * NSEC_PER_USEC, &ticks);
	deadline = mach_absolute_time() + ticks;
	while (mach_absolute_time() < deadline) { }
}

/* Pin 85 and 86 are the device-tree QUP5 pair. Do not infer which is SDA
 * from their list order: the caller must have independently verified it.
 * Opens only when QUP5 is clock-gated and both pins are plain inputs. */
static int __unused
mi8_speaker_i2c_open(struct mi8_speaker_i2c *s, unsigned sda, unsigned scl)
{
	vm_offset_t clocks;
	if (!s || s->acquired || !((sda == 85 && scl == 86) || (sda == 86 && scl == 85))) return -1;
	lck_mtx_lock(&mi8_spk_i2c_lock);
	clocks = gcc();
	if (!clocks || !(rd(clocks + MI8_SPK_I2C_ENGINE_CBCR) & MI8_SPK_CBCR_OFF)) goto fail;
	s->pin[0] = ml_io_map(MI8_SPK_TLMM_BASE + 0x1000ul * sda, 0x1000);
	s->pin[1] = ml_io_map(MI8_SPK_TLMM_BASE + 0x1000ul * scl, 0x1000);
	if (!s->pin[0] || !s->pin[1]) goto fail;
	for (unsigned i = 0; i < 2; i++) {
		s->saved_config[i] = rd(s->pin[i]);
		s->saved_output[i] = rd(s->pin[i] + 4) & 2u;
		if (s->saved_config[i] & (MI8_SPK_PIN_OUTPUT | MI8_SPK_PIN_FUNCTION)) goto fail;
		s->expected_config[i] = s->saved_config[i];
	}
	s->bus.context = s;
	s->bus.drive = mi8_speaker_i2c_drive;
	s->bus.level = mi8_speaker_i2c_level;
	s->bus.delay_us = mi8_speaker_i2c_wait;
	s->bus.half_period_us = 10;
	s->bus.stretch_limit_us = 10000;
	s->acquired = 1;
	return 0;
fail:
	for (unsigned i = 0; i < 2; i++) if (s->pin[i]) { ml_io_unmap(s->pin[i], 0x1000); s->pin[i] = 0; }
	lck_mtx_unlock(&mi8_spk_i2c_lock);
	return -1;
}

/* Restore only if nobody changed the pin configuration during our lease. */
static int __unused
mi8_speaker_i2c_close(struct mi8_speaker_i2c *s)
{
	int rc = 0;
	if (!s || !s->acquired) return -1;
	for (unsigned i = 0; i < 2; i++) {
		if (rd(s->pin[i]) != s->expected_config[i]) { rc = -1; continue; }
		/* Disable output before restoring its latch: it may have been high. */
		wr(s->pin[i], s->expected_config[i] & ~MI8_SPK_PIN_OUTPUT);
		settle();
		if (rd(s->pin[i]) & MI8_SPK_PIN_OUTPUT) { rc = -1; continue; }
		wr(s->pin[i] + 4, s->saved_output[i]);
		wr(s->pin[i], s->saved_config[i]);
		settle();
		if (rd(s->pin[i]) != s->saved_config[i]) rc = -1;
	}
	for (unsigned i = 0; i < 2; i++) { ml_io_unmap(s->pin[i], 0x1000); s->pin[i] = 0; }
	s->acquired = 0;
	lck_mtx_unlock(&mi8_spk_i2c_lock);
	return rc;
}
