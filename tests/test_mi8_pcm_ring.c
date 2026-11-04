/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "mi8-pcm-ring.h"

static struct mi8_pcm_ring ring;
enum { TOTAL = 200000 };

static void *producer(void *unused)
{
	uint32_t next = 0, values[113];
	(void)unused;
	while (next < TOTAL) {
		uint32_t count = TOTAL - next < 113 ? TOTAL - next : 113;
		for (uint32_t i = 0; i < count; i++) values[i] = next + i;
		uint32_t wrote = mi8_ring_write(&ring, (const int16_t *)values, count);
		next += wrote;
		if (!wrote) sched_yield();
	}
	return NULL;
}
static void *consumer(void *unused)
{
	uint32_t expected = 0, values[79];
	(void)unused;
	while (expected < TOTAL) {
		uint32_t n = mi8_ring_read(&ring, (int16_t *)values, 79);
		for (uint32_t i = 0; i < n; i++) assert(values[i] == expected++);
		if (!n) sched_yield();
	}
	return NULL;
}
int main(void)
{
	uint32_t in[20], out[20];
	pthread_t a, b;
	mi8_ring_init(&ring);
	assert(mi8_ring_read(&ring, (int16_t *)out, 20) == 0);
	for (uint32_t i = 0; i < 20; i++) in[i] = i;
	/* Exercise both ring wrap and unsigned counter wrap. */
	__atomic_store_n(&ring.head, UINT32_MAX - 3, __ATOMIC_RELAXED);
	__atomic_store_n(&ring.tail, UINT32_MAX - 3, __ATOMIC_RELAXED);
	assert(mi8_ring_write(&ring, (const int16_t *)in, 20) == 20);
	assert(mi8_ring_read(&ring, (int16_t *)out, 20) == 20 && !memcmp(in, out, sizeof(in)));
	mi8_ring_init(&ring);
	assert(pthread_create(&a, NULL, producer, NULL) == 0);
	assert(pthread_create(&b, NULL, consumer, NULL) == 0);
	assert(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
	assert(mi8_ring_read(&ring, (int16_t *)out, 20) == 0);
	puts("PCM ring: wrap, ordering and concurrent producer/consumer passed");
}
