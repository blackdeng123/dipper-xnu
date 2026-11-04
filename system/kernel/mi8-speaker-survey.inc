/* Read-only Mi 8 speaker survey; register locations are hardware facts.
 * References: https://github.com/torvalds/linux/blob/master/drivers/clk/qcom/gcc-sdm845.c
 *             https://github.com/torvalds/linux/blob/master/drivers/pinctrl/qcom/pinctrl-sdm845.c
 */
static void
mi8_speaker_survey(void)
{
	uint32_t master = rd(gcc + 0x17004), slave = rd(gcc + 0x17008);
	uint32_t engine = rd(gcc + 0x17620);
	const unsigned pins[] = {58, 59, 60, 61, 62, 63, 76, 85, 86};

	printf("mi8speaker: wrapper0 clock master=%#x slave=%#x se5=%#x votes=%#x\n",
	    master, slave, engine, rd(gcc + GCC_VOTE_APPS));
	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		unsigned pin = pins[i];
		vm_offset_t tile = pin < 64 ? TLMM_NORTH : 0x00100000UL;
		vm_offset_t p = ml_io_map(TLMM_PHYS + tile + 0x1000UL * pin, 0x1000);
		if (p) {
			printf("mi8speaker: gpio%u config=%#x io=%#x\n", pin, rd(p), rd(p + 4));
		} else {
			printf("mi8speaker: gpio%u mapping failed\n", pin);
		}
	}
	if (!((master | slave | engine) & CBCR_OFF)) {
		vm_offset_t se = ml_io_map(0x00894000UL, 0x4000);
		if (se) {
			printf("mi8speaker: se5 firmware=%#x status=%#x\n",
			    rd(se + GENI_FW_REVISION_RO), rd(se + GENI_STATUS));
		} else {
			printf("mi8speaker: se5 mapping failed\n");
		}
	} else {
		printf("mi8speaker: se5 skipped: required clock is off\n");
	}
}
