/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mi8-tas-init.h"

struct fixture { unsigned calls, delay; int mismatch; };
static int transfer(void *context, uint8_t addr, const uint8_t *write, size_t w, uint8_t *read, size_t r)
{
	struct fixture *f = context;
	assert(addr == 0x4c && write && w);
	f->calls++;
	if (r) { assert(w == 1 && r == 1); read[0] = f->mismatch ? 0 : 0xa0; }
	return 0;
}
static void delay_us(void *context, uint32_t us) { ((struct fixture *)context)->delay += us; }
int main(int argc, char **argv)
{
	uint8_t data[] = {
		'M','I','8','T','A','S','1',0, 4,0,0,0,
		0,0,0,0, 2,0,0,0, 0,0,
		0,0,0,0, 2,0,0,0, 127,0,
		0,0,0,0, 2,0,0,0, 1,1,
		100,0,0,0, 1,0,1,0, 3,0xa0
	};
	struct fixture f = { 0 };
	struct mi8_tas_init_io io = { &f, transfer, delay_us };
	uint32_t failed;
	assert(mi8_tas_init_validate(data, sizeof(data)) == 0);
	assert(mi8_tas_init_run(data, sizeof(data), &io, &failed) == 0 && f.calls == 4 && f.delay == 100);
	f.calls = 0; f.mismatch = 1;
	assert(mi8_tas_init_run(data, sizeof(data), &io, &failed) == -2 && failed == 3 && f.calls == 4);
	data[42] = 99; /* post-reset wait too short: reject before touching bus */
	f.calls = 0;
	assert(mi8_tas_init_run(data, sizeof(data), &io, &failed) == -1 && f.calls == 0);
	data[42] = 100; data[50] = 0x68; /* sticky fault read is forbidden */
	assert(mi8_tas_init_validate(data, sizeof(data)) == -1);
	data[46] = 2; data[48] = 0; data[50] = 4; /* power write is forbidden */
	f.calls = 0;
	assert(mi8_tas_init_run(data, sizeof(data), &io, &failed) == -1 && f.calls == 0);
	if (argc == 2) {
		FILE *file = fopen(argv[1], "rb");
		uint8_t *blob;
		long size;
		assert(file && fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && size < 1000000);
		assert(fseek(file, 0, SEEK_SET) == 0 && (blob = malloc((size_t)size)));
		assert(fread(blob, 1, (size_t)size, file) == (size_t)size);
		assert(mi8_tas_init_validate(blob, (size_t)size) == 0);
		free(blob); fclose(file);
		puts("private device transcript passes unpowered validation");
	}
	puts("TAS init: reset delay, checked reads and fault-register guard passed");
}
