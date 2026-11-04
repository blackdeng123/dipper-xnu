/* SPDX-License-Identifier: MIT
 * Glue for the unpowered, device-local TAS2557 initialization transcript.
 * This is not registered as an audio sink or automatically started.
 */
#include "mi8-tas-init.h"

static int
mi8_speaker_init_transfer(void *context, uint8_t address, const uint8_t *write,
    size_t write_size, uint8_t *read, size_t read_size)
{
	struct mi8_speaker_i2c *speaker = context;
	return mi8_i2c_transfer(&speaker->bus, address, write, write_size, read, read_size);
}

static void
mi8_speaker_init_delay(__unused void *context, uint32_t us)
{
	delay_for_interval(us, NSEC_PER_USEC);
}

/* Both bus and reset must be exclusively held by this worker. The caller
 * authenticates the provenance and digest of this phone's private transcript,
 * then asserts/releases hardware reset. This routine does not enable power,
 * unmute, configure I2S pins or verify speaker protection. On failure the
 * caller must reassert hardware reset before releasing the bus or memory. */
static int __unused
mi8_speaker_init_unpowered(struct mi8_speaker_i2c *speaker, struct mi8_speaker_reset *reset,
    const uint8_t *transcript, size_t bytes, uint32_t *failed_record)
{
	struct mi8_tas_init_io io = { speaker, mi8_speaker_init_transfer, mi8_speaker_init_delay };
	if (!speaker || !speaker->acquired || !reset || !reset->acquired || reset->asserted || reset->uncertain)
		return -1;
	return mi8_tas_init_run(transcript, bytes, &io, failed_record);
}
