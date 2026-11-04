/* SPDX-License-Identifier: APSL-2.0
 * Mi 8 XNU DMA adapter. Allocation and cache handling follow the existing
 * mi8_rproc.c and HackiOS audio driver (APSL-2.0):
 * https://github.com/oosexgang/hakios-xnu-kernel
 *
 * This adapter is deliberately not started by itself. Its owner must stop the
 * DSP before releasing a live mapping, and retain this structure and its
 * backing pages on any uncertainty. A quarantined mapping requires a reset.
 */
#include "mi8-smmu-program.h"

#define MI8_AUDIO_IOVA 0x10000000ull
#define MI8_AUDIO_DSP_ADDRESS (0x100000000ull | MI8_AUDIO_IOVA)
#define MI8_AUDIO_DMA_SIZE (11u * MI8_SMMU_PAGE_BYTES)

extern lck_mtx_t redwood_smmu_lock;

struct mi8_audio_dma {
	vm_offset_t allocation, mmio;
	vm_size_t allocation_size;
	struct mi8_smmu_snapshot snapshot;
	struct mi8_smmu_mapping mapping;
};

static int
mi8_audio_mmio_read(void *context, uint32_t offset, uint32_t *value)
{
	struct mi8_audio_dma *dma = context;
	if (!dma || !dma->mmio || !value || (offset & 3) || offset > 0x7fffcu) return -1;
	*value = rd(dma->mmio + offset);
	return 0;
}

static int
mi8_audio_mmio_write(void *context, uint32_t offset, uint32_t value)
{
	struct mi8_audio_dma *dma = context;
	if (!dma || !dma->mmio || (offset & 3) || offset > 0x7fffcu) return -1;
	wr(dma->mmio + offset, value);
	return 0;
}

static void
mi8_audio_mmio_wait(__unused void *context, uint32_t usec)
{
	delay_for_interval(usec, NSEC_PER_USEC);
}

static void
mi8_audio_dma_sync(__unused void *context, void *address, size_t length)
{
	vm_offset_t first = (vm_offset_t)address & ~(vm_offset_t)63;
	vm_offset_t end = (vm_offset_t)address + length;
	for (vm_offset_t line = first; line < end; line += 64)
		__asm__ volatile ("dc civac, %0" : : "r" (line) : "memory");
	__asm__ volatile ("dsb sy" : : : "memory");
}

/* No hardware access: allocate page tables and sample pages and verify every
 * physical address before exposing the buffer to the DSP. */
static int __unused
mi8_audio_dma_prepare(struct mi8_audio_dma *dma)
{
	uint64_t pa[11];
	if (!dma || dma->allocation || dma->mmio || dma->mapping.state) return -1;
	dma->allocation_size = round_page(MI8_AUDIO_DMA_SIZE);
	if (kmem_alloc(kernel_map, &dma->allocation, dma->allocation_size,
	    KMA_KOBJECT | KMA_ZERO | KMA_DATA, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) return -2;
	for (unsigned i = 0; i < 11; i++) pa[i] = kvtophys(dma->allocation + i * MI8_SMMU_PAGE_BYTES);
	if (mi8_smmu_audio_tables((uint64_t *)dma->allocation,
	    (uint64_t *)(dma->allocation + MI8_SMMU_PAGE_BYTES),
	    (uint64_t *)(dma->allocation + 2 * MI8_SMMU_PAGE_BYTES),
	    pa[0], pa[1], pa[2], MI8_AUDIO_IOVA, &pa[3])) {
		kmem_free(kernel_map, dma->allocation, dma->allocation_size);
		dma->allocation = 0; dma->allocation_size = 0;
		return -3;
	}
	mi8_audio_dma_sync(NULL, (void *)dma->allocation, 3 * MI8_SMMU_PAGE_BYTES);
	return 0;
}

/* Serialize selection and installation with camera and Wi-Fi. The valid SMR
 * reserves the chosen context after the lock is released. */
static int __unused
mi8_audio_dma_install(struct mi8_audio_dma *dma)
{
	struct mi8_smmu_io io = { dma, mi8_audio_mmio_read, mi8_audio_mmio_write, mi8_audio_mmio_wait };
	int rc;
	if (!dma || !dma->allocation || dma->mmio || dma->mapping.state) return -1;
	dma->mmio = ml_io_map(0x15000000u, 0x80000u);
	if (!dma->mmio) return -2;
	lck_mtx_lock(&redwood_smmu_lock);
	rc = mi8_smmu_snapshot_take(&io, &dma->snapshot);
	if (!rc) rc = mi8_smmu_install(&dma->mapping, &io, &dma->snapshot, kvtophys(dma->allocation));
	lck_mtx_unlock(&redwood_smmu_lock);
	if (rc && !dma->mapping.state) {
		ml_io_unmap(dma->mmio, 0x80000u); dma->mmio = 0;
	}
	return rc;
}

/* Only after the DSP has stopped and cannot touch these pages. Failure keeps
 * all memory and mappings alive for a full reset. */
static int __unused
mi8_audio_dma_release(struct mi8_audio_dma *dma)
{
	int rc = 0;
	if (!dma || !dma->allocation || dma->mapping.state == 1 || dma->mapping.state == 3) return -1;
	if (dma->mapping.state == 2) {
		lck_mtx_lock(&redwood_smmu_lock);
		rc = mi8_smmu_release(&dma->mapping);
		lck_mtx_unlock(&redwood_smmu_lock);
		if (rc) return -2;
	}
	if (dma->mmio) { ml_io_unmap(dma->mmio, 0x80000u); dma->mmio = 0; }
	kmem_free(kernel_map, dma->allocation, dma->allocation_size);
	dma->allocation = 0; dma->allocation_size = 0;
	return 0;
}
