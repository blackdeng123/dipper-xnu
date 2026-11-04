/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "mi8-smmu-program.h"

struct fake { uint32_t reg[0x80000 / 4]; unsigned writes, fail_at, forbidden_read; int fail_permanently, busy; };
static int fake_read(void *context, uint32_t offset, uint32_t *value)
{
	struct fake *f = context;
	assert(!(offset & 3) && offset < sizeof(f->reg));
	assert(!f->forbidden_read || offset != f->forbidden_read);
	if ((offset & 0xfff) == 0x7f4 && f->busy) { *value = 1; return 0; }
	*value = f->reg[offset / 4];
	return 0;
}
static int fake_write(void *context, uint32_t offset, uint32_t value)
{
	struct fake *f = context;
	assert(!(offset & 3) && offset < sizeof(f->reg));
	f->writes++;
	if (f->fail_at && (f->writes == f->fail_at || (f->fail_permanently && f->writes >= f->fail_at))) return -1;
	f->reg[offset / 4] = value;
	return 0;
}
static void fake_wait(void *context, uint32_t us) { (void)context; assert(us == 1000); }
static void setup(struct fake *f, struct mi8_smmu_snapshot *s)
{
	memset(f, 0, sizeof(*f)); memset(s, 0, sizeof(*s));
	s->idr0 = 4; s->idr1 = 0x50000004; s->window = 0x80000;
	f->reg[0x20 / 4] = s->idr0; f->reg[0x24 / 4] = s->idr1;
	for (unsigned i = 0; i < 4; i++) s->s2cr[i] = f->reg[MI8_SMMU_S2CR(i) / 4] = 0x200ff;
	f->reg[(0x40000 + 0x10) / 4] = 0x1234;
	f->reg[(0x40000 + 0x20) / 4] = 0xabc000;
	f->reg[(0x40000 + 0x38) / 4] = 0x12345678;
}
int main(void)
{
	static struct fake f;
	static struct mi8_smmu_snapshot s;
	struct mi8_smmu_mapping m = { 0 };
	struct mi8_smmu_io io = { &f, fake_read, fake_write, fake_wait };
	uint32_t slot, context;
	setup(&f, &s);
	f.reg[MI8_SMMU_SMR(0) / 4] = 0x80001234; f.reg[MI8_SMMU_S2CR(0) / 4] = 3;
	f.forbidden_read = 0x43000;
	assert(mi8_smmu_snapshot_take(&io, &s) == 0);
	assert(mi8_smmu_pick(MI8_SMMU_STREAM, s.smr, s.s2cr, s.idr0 & 0xff,
	    s.sctlr, s.cbar, 4, &slot, &context) == 0 && slot == 1 && context == 2);
	f.reg[MI8_SMMU_SMR(0) / 4] = 0x800f1820;
	assert(mi8_smmu_snapshot_take(&io, &s) == -2 && f.writes == 0);
	setup(&f, &s);
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == 0);
	assert(m.state == 2 && m.slot == 0 && m.context == 0);
	assert(f.reg[MI8_SMMU_SMR(0) / 4] == (0x80000000u | MI8_SMMU_STREAM));
	assert(f.reg[(0x40000 + 0x20) / 4] == 0x10000000);
	assert(f.reg[(0x40000 + 0x24) / 4] == 1);
	assert(f.reg[(0x40000 + 0x10) / 4] == 0x38011);
	assert(mi8_smmu_release(&m) == 0 && !m.state);
	assert(f.reg[MI8_SMMU_SMR(0) / 4] == 0 && f.reg[MI8_SMMU_S2CR(0) / 4] == 0x200ff);
	assert(f.reg[(0x40000 + 0x10) / 4] == 0x1234 && f.reg[(0x40000 + 0x20) / 4] == 0xabc000 &&
	    f.reg[(0x40000 + 0x38) / 4] == 0x12345678);
	setup(&f, &s); memset(&m, 0, sizeof(m)); f.fail_at = 4;
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == -5 && m.state == 0);
	assert(f.reg[MI8_SMMU_SMR(0) / 4] == 0 && f.reg[(0x40000 + 0x10) / 4] == 0x1234);
	setup(&f, &s); memset(&m, 0, sizeof(m)); f.fail_at = 4; f.fail_permanently = 1;
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == -6 && m.state == 3);
	setup(&f, &s); memset(&m, 0, sizeof(m)); f.reg[(0x40000 + 0x58) / 4] = 2;
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == -4 && f.writes == 0);
	setup(&f, &s); memset(&m, 0, sizeof(m));
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == 0);
	f.reg[MI8_SMMU_SMR(0) / 4] = 0x80001234;
	unsigned before = f.writes;
	assert(mi8_smmu_release(&m) == -2 && m.state == 3 && f.writes == before);
	setup(&f, &s); memset(&m, 0, sizeof(m)); f.busy = 1;
	assert(mi8_smmu_install(&m, &io, &s, 0x110000000ull) == -6 && m.state == 3);
	puts("Mi 8 SMMU install, readback, rollback and ownership passed");
}
