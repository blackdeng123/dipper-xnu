/* SPDX-License-Identifier: APSL-2.0
 * Mi 8 TAS2557 reset GPIO adapter; GPIO register access follows the project's
 * mi8_fts.c (copyright 2026 yanei and dipper-xnu contributors, MIT; see
 * LICENSES/MI8-packaging-MIT.txt). Device facts: Mi 8 device tree and
 * https://www.ti.com/lit/ds/symlink/tas2557.pdf (hardware and software reset).
 * No startup path invokes this adapter yet.
 */
#define MI8_SPK_RESET_PHYS 0x0354c000ul /* TLMM center, GPIO 76 */
#define MI8_SPK_RESET_OUTPUT (1u << 9)
#define MI8_SPK_RESET_FUNCTION (15u << 2)

static LCK_GRP_DECLARE(mi8_spk_reset_group, "mi8-speaker-reset");
static LCK_MTX_DECLARE(mi8_spk_reset_lock, &mi8_spk_reset_group);

struct mi8_speaker_reset {
	vm_offset_t pin;
	uint32_t saved_config, saved_output, active_config;
	int acquired, asserted, uncertain;
};

/* Keep ownership until close; caller must also exclude all other GPIO users. */
static int __unused
mi8_speaker_reset_open(struct mi8_speaker_reset *s)
{
	if (!s || s->acquired || s->uncertain) return -1;
	lck_mtx_lock(&mi8_spk_reset_lock);
	s->pin = ml_io_map(MI8_SPK_RESET_PHYS, 0x1000);
	if (!s->pin) goto fail;
	s->saved_config = rd(s->pin);
	s->saved_output = rd(s->pin + 4) & 2u;
	if (s->saved_config & (MI8_SPK_RESET_OUTPUT | MI8_SPK_RESET_FUNCTION)) goto fail;
	s->active_config = (s->saved_config & ~0x3ffu) | MI8_SPK_RESET_OUTPUT;
	/* Put the latch low before enabling output; hold reset for at least 5 ms. */
	wr(s->pin + 4, 0);
	wr(s->pin, s->active_config);
	settle();
	s->acquired = 1;
	s->asserted = 1;
	if (rd(s->pin) != s->active_config || (rd(s->pin + 4) & 3u)) {
		s->uncertain = 1;
		return -2; /* keep the mapping and mutex for explicit recovery */
	}
	delay_for_interval(5, NSEC_PER_MSEC);
	return 0;
fail:
	if (s->pin) { ml_io_unmap(s->pin, 0x1000); s->pin = 0; }
	lck_mtx_unlock(&mi8_spk_reset_lock);
	return -1;
}

/* Release reset only after bus setup. The chip still requires a software
 * reset and at least 100 us without accesses, per TI's data sheet. */
static int __unused
mi8_speaker_reset_release(struct mi8_speaker_reset *s)
{
	if (!s || !s->acquired || !s->asserted || s->uncertain || rd(s->pin) != s->active_config) return -1;
	wr(s->pin + 4, 2);
	settle();
	if ((rd(s->pin + 4) & 3u) != 3u) { s->uncertain = 1; return -2; }
	s->asserted = 0;
	delay_for_interval(2, NSEC_PER_MSEC);
	return 0;
}

/* Reset the amplifier before restoring the original input configuration.
 * Refuse to touch the pin when another owner changed it. */
static int __unused
mi8_speaker_reset_close(struct mi8_speaker_reset *s)
{
	if (!s || !s->acquired || rd(s->pin) != s->active_config) return -1;
	wr(s->pin + 4, 0);
	settle();
	if (rd(s->pin + 4) & 3u) { s->uncertain = 1; return -2; }
	delay_for_interval(5, NSEC_PER_MSEC);
	wr(s->pin, s->active_config & ~MI8_SPK_RESET_OUTPUT);
	settle();
	if (rd(s->pin) & MI8_SPK_RESET_OUTPUT) { s->uncertain = 1; return -2; }
	wr(s->pin + 4, s->saved_output);
	wr(s->pin, s->saved_config);
	settle();
	if (rd(s->pin) != s->saved_config) { s->uncertain = 1; return -2; }
	ml_io_unmap(s->pin, 0x1000);
	s->pin = 0; s->acquired = 0; s->asserted = 0; s->uncertain = 0;
	lck_mtx_unlock(&mi8_spk_reset_lock);
	return 0;
}
