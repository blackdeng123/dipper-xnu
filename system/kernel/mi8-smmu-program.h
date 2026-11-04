/* SPDX-License-Identifier: APSL-2.0
 * Register sequencing derived from redwood_sound.c in
 * https://github.com/oosexgang/hakios-xnu-kernel (APSL-2.0) and this
 * project's mi8_rproc.c Wi-Fi SMMU support. Not enabled by the probe.
 */
#ifndef MI8_SMMU_PROGRAM_H
#define MI8_SMMU_PROGRAM_H
#include "mi8-smmu-pages.h"

/* Offsets from apps-smmu@0x15000000. The caller supplies ordered MMIO,
 * synchronizes the prepared page tables, holds the shared SMMU lock from
 * snapshot through installation, and takes it again for release. Other SMMU
 * owners must honor that lock and the valid stream mapping. The DSP and DMA
 * must stop before release; otherwise restoring a mapping can corrupt memory. */
struct mi8_smmu_io {
	void *context;
	int (*read)(void *, uint32_t, uint32_t *);
	int (*write)(void *, uint32_t, uint32_t);
	void (*wait_us)(void *, uint32_t);
};

struct mi8_smmu_snapshot {
	uint32_t scr0, idr0, idr1, window;
	uint32_t smr[256], s2cr[256], sctlr[256], cbar[256];
};

struct mi8_smmu_mapping {
	struct mi8_smmu_io io;
	struct mi8_smmu_geometry geometry;
	uint32_t slot, context, old_smr, old_s2cr;
	uint32_t old_cbar, old_cba2r, old_sctlr;
	uint32_t old_tcr2, old_tcr, old_ttbr0_lo, old_ttbr0_hi;
	uint32_t old_ttbr1_lo, old_ttbr1_hi, old_mair0, old_mair1;
	uint32_t state; /* 0 untouched/restored; 1 installing; 2 active; 3 quarantined; zero before first use */
};

#define MI8_SMMU_STREAM 0x1821u
#define MI8_SMMU_SMR(n) (0x800u + 4u * (n))
#define MI8_SMMU_S2CR(n) (0xc00u + 4u * (n))
#define MI8_SMMU_CBAR(g,n) ((g).page_size + 4u * (n))
#define MI8_SMMU_CBA2R(g,n) ((g).page_size + 0x800u + 4u * (n))
#define MI8_SMMU_CB(g,n) ((g).context_base + (g).page_size * (n))

/* Snapshot under the same shared lock used for installation. Avoid reading
 * context banks owned by active streams and stop after finding one free bank.
 * This routine is not enabled by the existing diagnostic boot option. */
static inline int
mi8_smmu_snapshot_take(const struct mi8_smmu_io *io, struct mi8_smmu_snapshot *snapshot)
{
	struct mi8_smmu_geometry geometry;
	uint8_t used[256] = { 0 };
	uint32_t matches;
	if (!io || !io->read || !snapshot) return -1;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->window = 0x80000;
	if (io->read(io->context, 0x00, &snapshot->scr0) || io->read(io->context, 0x20, &snapshot->idr0) ||
	    io->read(io->context, 0x24, &snapshot->idr1) ||
	    mi8_smmu_geometry(snapshot->scr0, snapshot->idr1, snapshot->window, &geometry)) return -1;
	matches = snapshot->idr0 & 0xff;
	for (uint32_t i = 0; i < matches; i++) {
		uint32_t id, mask;
		if (io->read(io->context, MI8_SMMU_SMR(i), &snapshot->smr[i]) ||
		    io->read(io->context, MI8_SMMU_S2CR(i), &snapshot->s2cr[i])) return -1;
		if (!(snapshot->smr[i] & 0x80000000u)) continue;
		id = snapshot->smr[i] & 0x7fff;
		mask = (snapshot->smr[i] >> 16) & 0x7fff;
		if (((id ^ MI8_SMMU_STREAM) & ~mask) == 0) return -2;
		used[snapshot->s2cr[i] & 0xff] = 1;
	}
	for (uint32_t i = 0; i < geometry.contexts; i++) {
		snapshot->sctlr[i] = UINT32_MAX;
		snapshot->cbar[i] = UINT32_MAX;
	}
	for (uint32_t i = geometry.contexts; i-- > 0;) {
		if (used[i]) continue;
		if (io->read(io->context, MI8_SMMU_CBAR(geometry, i), &snapshot->cbar[i]) ||
		    io->read(io->context, MI8_SMMU_CB(geometry, i), &snapshot->sctlr[i])) return -1;
		if (!snapshot->cbar[i] && !snapshot->sctlr[i]) return 0;
	}
	return -3;
}

/* Restore all registers we touched, leaving the old stream mapping until last.
 * A nonzero result means restoration is uncertain and backing memory must remain
 * allocated until a full device reset. */
static inline int
mi8_smmu_restore(struct mi8_smmu_mapping *m)
{
	uint32_t cb = MI8_SMMU_CB(m->geometry, m->context);
	int failed = 0;
#define MI8_RESTORE(off, val) do { if (m->io.write(m->io.context, (off), (val))) failed = 1; } while (0)
	MI8_RESTORE(MI8_SMMU_SMR(m->slot), m->old_smr & ~0x80000000u);
	MI8_RESTORE(MI8_SMMU_S2CR(m->slot), m->old_s2cr);
	MI8_RESTORE(cb + 0x00, 0);
	MI8_RESTORE(cb + 0x610, m->context);
	MI8_RESTORE(cb + 0x7f0, 1);
	for (unsigned i = 0; i < 100; i++) {
		uint32_t status;
		if (m->io.read(m->io.context, cb + 0x7f4, &status)) { failed = 1; break; }
		if (!(status & 1)) break;
		if (i == 99) { failed = 1; break; }
		m->io.wait_us(m->io.context, 1000);
	}
	MI8_RESTORE(cb + 0x10, m->old_tcr2);
	MI8_RESTORE(cb + 0x30, m->old_tcr);
	MI8_RESTORE(cb + 0x20, m->old_ttbr0_lo);
	MI8_RESTORE(cb + 0x24, m->old_ttbr0_hi);
	MI8_RESTORE(cb + 0x28, m->old_ttbr1_lo);
	MI8_RESTORE(cb + 0x2c, m->old_ttbr1_hi);
	MI8_RESTORE(cb + 0x38, m->old_mair0);
	MI8_RESTORE(cb + 0x3c, m->old_mair1);
	MI8_RESTORE(MI8_SMMU_CBA2R(m->geometry, m->context), m->old_cba2r);
	MI8_RESTORE(MI8_SMMU_CBAR(m->geometry, m->context), m->old_cbar);
	MI8_RESTORE(cb + 0x00, m->old_sctlr);
	MI8_RESTORE(MI8_SMMU_SMR(m->slot), m->old_smr);
#undef MI8_RESTORE
	m->state = failed ? 3 : 0;
	return failed ? -1 : 0;
}

static inline int
mi8_smmu_install(struct mi8_smmu_mapping *m, const struct mi8_smmu_io *io,
    const struct mi8_smmu_snapshot *snapshot, uint64_t root_pa)
{
	uint32_t matches, cb, v, saved[9], smr, s2cr, ttbr_lo, ttbr_hi;
	uint64_t table_root;
	int error;
	if (!m || !io || !io->read || !io->write || !io->wait_us || !snapshot || m->state ||
	    snapshot->window != 0x80000u ||
	    !mi8_smmu_valid_pa(root_pa) || root_pa >= (1ull << 36)) return -1;
	if (mi8_smmu_geometry(snapshot->scr0, snapshot->idr1, snapshot->window, &m->geometry)) return -1;
	matches = snapshot->idr0 & 0xff;
	error = mi8_smmu_pick(MI8_SMMU_STREAM, snapshot->smr, snapshot->s2cr, matches,
	    snapshot->sctlr, snapshot->cbar, m->geometry.contexts, &m->slot, &m->context);
	if (error) return error;
	m->io = *io; cb = MI8_SMMU_CB(m->geometry, m->context);
	m->old_smr = snapshot->smr[m->slot]; m->old_s2cr = snapshot->s2cr[m->slot];
	m->old_sctlr = snapshot->sctlr[m->context]; m->old_cbar = snapshot->cbar[m->context];
	/* Recheck ownership immediately before touching registers. */
	if (io->read(io->context, 0x00, &v) || v != snapshot->scr0 ||
	    io->read(io->context, 0x20, &v) || v != snapshot->idr0 ||
	    io->read(io->context, 0x24, &v) || v != snapshot->idr1 ||
	    io->read(io->context, MI8_SMMU_SMR(m->slot), &smr) ||
	    io->read(io->context, MI8_SMMU_S2CR(m->slot), &s2cr) ||
	    io->read(io->context, cb, &v) || smr != m->old_smr || s2cr != m->old_s2cr || v != m->old_sctlr ||
	    io->read(io->context, MI8_SMMU_CBAR(m->geometry, m->context), &v) || v != m->old_cbar ||
	    io->read(io->context, cb + 0x58, &v) || v != 0) return -4;
	const uint32_t offsets[9] = { MI8_SMMU_CBA2R(m->geometry, m->context), cb + 0x10, cb + 0x30,
	    cb + 0x20, cb + 0x24, cb + 0x28, cb + 0x2c, cb + 0x38, cb + 0x3c };
	for (unsigned i = 0; i < 9; i++) if (io->read(io->context, offsets[i], &saved[i])) return -4;
	m->old_cba2r = saved[0]; m->old_tcr2 = saved[1]; m->old_tcr = saved[2];
	m->old_ttbr0_lo = saved[3]; m->old_ttbr0_hi = saved[4];
	m->old_ttbr1_lo = saved[5]; m->old_ttbr1_hi = saved[6];
	m->old_mair0 = saved[7]; m->old_mair1 = saved[8];
	table_root = root_pa | (uint64_t)m->context << 48;
	m->state = 1;
#define MI8_WRITE(off, val) do { if (io->write(io->context, (off), (val))) goto rollback; } while (0)
	MI8_WRITE(cb, 0);
	MI8_WRITE(MI8_SMMU_CBA2R(m->geometry, m->context), 1);
	MI8_WRITE(MI8_SMMU_CBAR(m->geometry, m->context), 0x1f300);
	MI8_WRITE(cb + 0x10, 0x38011);
	MI8_WRITE(cb + 0x30, 0x803519);
	MI8_WRITE(cb + 0x20, (uint32_t)table_root);
	MI8_WRITE(cb + 0x24, (uint32_t)(table_root >> 32));
	MI8_WRITE(cb + 0x28, 0); MI8_WRITE(cb + 0x2c, 0);
	MI8_WRITE(cb + 0x38, 0xf404ff44); MI8_WRITE(cb + 0x3c, 0);
	MI8_WRITE(cb + 0x610, m->context); MI8_WRITE(cb + 0x7f0, 1);
	for (unsigned i = 0; ; i++) {
		if (io->read(io->context, cb + 0x7f4, &v)) goto rollback;
		if (!(v & 1)) break;
		if (i == 99) goto rollback;
		io->wait_us(io->context, 1000);
	}
	MI8_WRITE(cb, 0x1127);
	MI8_WRITE(MI8_SMMU_S2CR(m->slot), m->context);
	MI8_WRITE(MI8_SMMU_SMR(m->slot), 0x80000000u | MI8_SMMU_STREAM);
	if (io->read(io->context, MI8_SMMU_SMR(m->slot), &smr) ||
	    io->read(io->context, MI8_SMMU_S2CR(m->slot), &s2cr) ||
	    io->read(io->context, cb + 0x20, &ttbr_lo) || io->read(io->context, cb + 0x24, &ttbr_hi) ||
	    io->read(io->context, cb, &v) || smr != (0x80000000u | MI8_SMMU_STREAM) ||
	    (s2cr & 0x300ffu) != m->context || !(v & 1) ||
	    ttbr_lo != (uint32_t)table_root || ttbr_hi != (uint32_t)(table_root >> 32)) goto rollback;
	m->state = 2;
#undef MI8_WRITE
	return 0;
rollback:
#undef MI8_WRITE
	return mi8_smmu_restore(m) ? -6 : -5;
}

/* Refuse to clobber a mapping another owner changed. */
static inline int
mi8_smmu_release(struct mi8_smmu_mapping *m)
{
	uint32_t smr, s2cr;
	if (!m || m->state != 2) return -1;
	if (m->io.read(m->io.context, MI8_SMMU_SMR(m->slot), &smr) ||
	    m->io.read(m->io.context, MI8_SMMU_S2CR(m->slot), &s2cr) ||
	    smr != (0x80000000u | MI8_SMMU_STREAM) || (s2cr & 0x300ffu) != m->context) {
		m->state = 3;
		return -2;
	}
	return mi8_smmu_restore(m);
}
#endif
