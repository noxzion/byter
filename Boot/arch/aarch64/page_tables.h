#ifndef BYTER_AARCH64_PAGE_TABLES_H
#define BYTER_AARCH64_PAGE_TABLES_H

#include <stdint.h>

#define BYTER_AARCH64_HHDM_OFFSET 0xffff800000000000ULL

struct byter_aarch64_range {
    uint64_t base;
    uint64_t length;
};

struct byter_aarch64_load_segment {
    uint64_t physical_base;
    uint64_t virtual_base;
    uint64_t length;
    uint32_t flags;
};

struct byter_aarch64_page_tables {
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t hhdm_offset;
};

int byter_aarch64_build_page_tables(
    uint8_t *shim, uint64_t shim_size,
    const struct byter_aarch64_range *memory, uint32_t memory_count,
    struct byter_aarch64_range framebuffer, int has_framebuffer,
    const struct byter_aarch64_load_segment *segments, uint32_t segment_count,
    struct byter_aarch64_page_tables *result);

#endif
