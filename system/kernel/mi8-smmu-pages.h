/* SPDX-License-Identifier: APSL-2.0
 * Audio SMMU page table construction, adapted from redwood_sound.c:
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0).
 * This file constructs memory only; it does not touch SMMU registers.
 */
#ifndef MI8_SMMU_PAGES_H
#define MI8_SMMU_PAGES_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MI8_SMMU_PAGE_BYTES 4096u
#define MI8_SMMU_TABLE_ENTRIES 512u
#define MI8_SMMU_AUDIO_PAGES 8u
#define MI8_SMMU_ADDR_MASK 0x0000fffffffff000ull
#define MI8_SMMU_AUDIO_PTE (3ull | 1ull << 6 | 2ull << 8 | 1ull << 10 | 1ull << 11)

struct mi8_smmu_geometry { uint32_t page_size, pages_per_half, context_base, contexts; };

/* Verify IDR1 geometry against the device-tree register window before an MMIO
 * user derives context addresses. 16-bit stream matching needs a separate
 * implementation and is intentionally rejected. */
static inline int
mi8_smmu_geometry(uint32_t scr0, uint32_t idr1, uint32_t window, struct mi8_smmu_geometry *out)
{
	uint32_t page, pages, contexts;
	if (!out || (scr0 & 1u) || (scr0 & (1u << 3))) return -1;
	page = idr1 & (1u << 31) ? 65536u : 4096u;
	pages = 2u << ((idr1 >> 28) & 7u);
	contexts = idr1 & 0xffu;
	if (!contexts || contexts > pages || (uint64_t)page * pages * 2 != window) return -1;
	out->page_size = page; out->pages_per_half = pages;
	out->context_base = page * pages; out->contexts = contexts;
	return 0;
}

/* All inputs are snapshots read under the shared SMMU lock. Reject an existing
 * match for the audio SID rather than replacing an owner's mapping. A context
 * is considered free only if no valid slot names it and both registers are 0. */
static inline int
mi8_smmu_pick(uint32_t stream, const uint32_t *smr, const uint32_t *s2cr, size_t matches,
    const uint32_t *sctlr, const uint32_t *cbar, size_t contexts, uint32_t *slot, uint32_t *context)
{
	uint8_t used[256] = { 0 };
	uint32_t candidate = UINT32_MAX;
	if (!smr || !s2cr || !sctlr || !cbar || !slot || !context || stream > 0x7fff ||
	    matches > 256 || contexts == 0 || contexts > 256) return -1;
	for (size_t i = 0; i < matches; i++) {
		uint32_t id = smr[i] & 0x7fff, mask = (smr[i] >> 16) & 0x7fff;
		if (smr[i] & 0x80000000u) {
			used[s2cr[i] & 0xff] = 1;
			if (((id ^ stream) & ~mask) == 0) return -2;
		} else if (candidate == UINT32_MAX) candidate = (uint32_t)i;
	}
	if (candidate == UINT32_MAX) return -3;
	for (size_t i = 0; i < contexts; i++) {
		if (!used[i] && sctlr[i] == 0 && cbar[i] == 0) {
			*slot = candidate; *context = (uint32_t)i;
			return 0;
		}
	}
	return -3;
}

static inline int
mi8_smmu_valid_pa(uint64_t address)
{
	return address != 0 && address < (1ull << 36) && (address & ~MI8_SMMU_ADDR_MASK) == 0;
}

/* Construct one 39-bit IOVA mapping. Eight data pages must fit into a single
 * 2 MiB L3 table. The caller flushes the tables before installing them. */
static inline int
mi8_smmu_audio_tables(uint64_t *root, uint64_t *l2, uint64_t *l3,
    uint64_t root_pa, uint64_t l2_pa, uint64_t l3_pa,
    uint64_t iova, const uint64_t pages[MI8_SMMU_AUDIO_PAGES])
{
	if (!root || !l2 || !l3 || !pages || root == l2 || root == l3 || l2 == l3 ||
	    !mi8_smmu_valid_pa(root_pa) || !mi8_smmu_valid_pa(l2_pa) || !mi8_smmu_valid_pa(l3_pa) ||
	    root_pa == l2_pa || root_pa == l3_pa || l2_pa == l3_pa ||
	    (iova & (MI8_SMMU_PAGE_BYTES - 1)) || iova >= (1ull << 32) ||
	    (iova & 0x1fffffull) + MI8_SMMU_AUDIO_PAGES * MI8_SMMU_PAGE_BYTES > 0x200000ull) return -1;
	for (unsigned i = 0; i < MI8_SMMU_AUDIO_PAGES; i++) {
		if (!mi8_smmu_valid_pa(pages[i]) || pages[i] == root_pa || pages[i] == l2_pa || pages[i] == l3_pa) return -1;
		for (unsigned j = 0; j < i; j++) if (pages[i] == pages[j]) return -1;
	}
	memset(root, 0, MI8_SMMU_PAGE_BYTES);
	memset(l2, 0, MI8_SMMU_PAGE_BYTES);
	memset(l3, 0, MI8_SMMU_PAGE_BYTES);
	root[(iova >> 30) & 0x1ff] = l2_pa | 3;
	l2[(iova >> 21) & 0x1ff] = l3_pa | 3;
	for (unsigned i = 0; i < MI8_SMMU_AUDIO_PAGES; i++)
		l3[((iova >> 12) & 0x1ff) + i] = pages[i] | MI8_SMMU_AUDIO_PTE;
	return 0;
}
#endif
