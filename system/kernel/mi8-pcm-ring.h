/* SPDX-License-Identifier: MIT
 * Single-producer/single-consumer stereo PCM queue. The caller serializes
 * producers (redwood_audio's audio_lock does so) and has one worker consumer.
 */
#ifndef MI8_PCM_RING_H
#define MI8_PCM_RING_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MI8_RING_FRAMES 8192u
#define MI8_RING_FRAME_BYTES 4u

struct mi8_pcm_ring {
	uint32_t head, tail;
	uint8_t data[MI8_RING_FRAMES * MI8_RING_FRAME_BYTES];
};

/* Must run before the producer or consumer begins, or after both have stopped. */
static inline void
mi8_ring_init(struct mi8_pcm_ring *ring)
{
	__atomic_store_n(&ring->head, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&ring->tail, 0, __ATOMIC_RELAXED);
}

/* Does not wait. Return the number of whole stereo frames accepted. */
static inline uint32_t
mi8_ring_write(struct mi8_pcm_ring *ring, const int16_t *samples, uint32_t frames)
{
	uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
	uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
	uint32_t used = head - tail, take, at, first;
	if (!samples || used > MI8_RING_FRAMES) return 0;
	take = frames < MI8_RING_FRAMES - used ? frames : MI8_RING_FRAMES - used;
	at = head % MI8_RING_FRAMES;
	first = take < MI8_RING_FRAMES - at ? take : MI8_RING_FRAMES - at;
	memcpy(ring->data + at * MI8_RING_FRAME_BYTES, samples, first * MI8_RING_FRAME_BYTES);
	memcpy(ring->data, (const uint8_t *)samples + first * MI8_RING_FRAME_BYTES,
	    (take - first) * MI8_RING_FRAME_BYTES);
	__atomic_store_n(&ring->head, head + take, __ATOMIC_RELEASE);
	return take;
}

/* Called only by the worker. */
static inline uint32_t
mi8_ring_read(struct mi8_pcm_ring *ring, int16_t *samples, uint32_t capacity)
{
	uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
	uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
	uint32_t available = head - tail, take, at, first;
	if (!samples || available > MI8_RING_FRAMES) return 0;
	take = capacity < available ? capacity : available;
	at = tail % MI8_RING_FRAMES;
	first = take < MI8_RING_FRAMES - at ? take : MI8_RING_FRAMES - at;
	memcpy(samples, ring->data + at * MI8_RING_FRAME_BYTES, first * MI8_RING_FRAME_BYTES);
	memcpy((uint8_t *)samples + first * MI8_RING_FRAME_BYTES, ring->data,
	    (take - first) * MI8_RING_FRAME_BYTES);
	__atomic_store_n(&ring->tail, tail + take, __ATOMIC_RELEASE);
	return take;
}
#endif
