/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-smmu-pages.h"

int main(void)
{
	uint64_t root[512], l2[512], l3[512], pages[8];
	struct mi8_smmu_geometry geometry;
	uint32_t smr[4] = { 0x80001234, 0, 0, 0 }, s2cr[4] = { 2, 0x200ff, 0x200ff, 0x200ff };
	uint32_t sctlr[4] = { 1, 0, 0, 0 }, cbar[4] = { 0, 5, 0, 0 }, slot, context;
	/* SDM845's live DT window is 512 KiB; 64 4 KiB pages per half. */
	assert(mi8_smmu_geometry(0, 0x5000002d, 0x80000, &geometry) == 0);
	assert(geometry.page_size == 4096 && geometry.context_base == 0x40000 && geometry.contexts == 45);
	assert(mi8_smmu_geometry(0, 0x5000002d, 0x100000, &geometry) == -1);
	assert(mi8_smmu_geometry(8, 0x5000002d, 0x80000, &geometry) == -1);
	assert(mi8_smmu_pick(0x1821, smr, s2cr, 4, sctlr, cbar, 4, &slot, &context) == 0);
	assert(slot == 1 && context == 3);
	smr[0] = 0x800f1820;
	assert(mi8_smmu_pick(0x1821, smr, s2cr, 4, sctlr, cbar, 4, &slot, &context) == -2);
	for (unsigned i = 0; i < 8; i++) pages[i] = 0x120000000ull + i * 0x2000;
	memset(root, 0xa5, sizeof(root)); memset(l2, 0xa5, sizeof(l2)); memset(l3, 0xa5, sizeof(l3));
	assert(mi8_smmu_audio_tables(root, l2, l3, 0x110000000ull, 0x110001000ull, 0x110002000ull,
	    0x101ff000ull, pages) == -1 && root[0] == 0xa5a5a5a5a5a5a5a5ull);
	assert(mi8_smmu_audio_tables(root, l2, l3, 0x110000000ull, 0x110001000ull, 0x110002000ull,
	    0x10000000ull, pages) == 0);
	assert(root[0] == (0x110001000ull | 3));
	assert(l2[0x80] == (0x110002000ull | 3));
	for (unsigned i = 0; i < 8; i++) assert(l3[i] == (pages[i] | MI8_SMMU_AUDIO_PTE));
	assert(root[1] == 0 && l2[0x81] == 0 && l3[8] == 0);
	pages[7] = pages[0];
	assert(mi8_smmu_audio_tables(root, l2, l3, 0x110000000ull, 0x110001000ull, 0x110002000ull,
	    0x10000000ull, pages) == -1);
	puts("Mi 8 SMMU geometry, ownership and audio page tables passed");
}
