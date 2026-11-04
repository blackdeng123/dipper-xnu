/* SPDX-License-Identifier: MIT
 * Explicitly requested, unpowered Mi 8 speaker initialization test. The
 * optional header contains device-private data and must not be distributed.
 */
#include "mi8-tas-private.h"

static void
mi8_speaker_probe_thread(__unused void *parameter, __unused wait_result_t result)
{
#if MI8_TAS_PRIVATE_AVAILABLE
	struct mi8_speaker_reset reset = { 0 };
	struct mi8_speaker_i2c bus = { 0 };
	uint32_t failed = UINT32_MAX;
	int rc, reset_close = 0, bus_close = 0;
	/* Let the user-state restore and early boot complete before GPIO changes. */
	delay_for_interval(60, NSEC_PER_SEC);
	say("mi8speaker: unpowered init probe begins\n");
	if (mi8_tas_init_validate(mi8_tas_private_data, sizeof(mi8_tas_private_data))) {
		say("mi8speaker: private transcript validation failed; no GPIO touched\n");
		goto done;
	}
	rc = mi8_speaker_reset_open(&reset);
	if (rc) { say("mi8speaker: reset open failed %d\n", rc); goto cleanup; }
	/* The device tree lists GPIO 85,86 for QUP5; the lane order is an
	 * explicit experiment, not yet verified by the iOS driver. */
	rc = mi8_speaker_i2c_open(&bus, 85, 86);
	if (rc) { say("mi8speaker: I2C open failed %d\n", rc); goto cleanup; }
	rc = mi8_speaker_reset_release(&reset);
	if (rc) { say("mi8speaker: reset release failed %d\n", rc); goto cleanup; }
	say("mi8speaker: applying unpowered init transcript\n");
	rc = mi8_speaker_init_unpowered(&bus, &reset, mi8_tas_private_data,
	    sizeof(mi8_tas_private_data), &failed);
	say("mi8speaker: unpowered init result %d failed record %u\n", rc, failed);
cleanup:
	if (reset.acquired) reset_close = mi8_speaker_reset_close(&reset);
	if (bus.acquired) bus_close = mi8_speaker_i2c_close(&bus);
	say("mi8speaker: cleanup reset=%d bus=%d\n", reset_close, bus_close);
done:
#else
	say("mi8speaker: private transcript absent; probe skipped\n");
#endif
	thread_terminate(current_thread());
}
