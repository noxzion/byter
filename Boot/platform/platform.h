#ifndef BYTER_PLATFORM_H
#define BYTER_PLATFORM_H

#include <stdint.h>
#include "byter.h"

#define BYTER_PLATFORM_MAX_RANGES 512U

typedef struct {
    uint64_t base;
    uint64_t length;
} byter_platform_range;

typedef struct {
    uint64_t address, size, width, height, pitch;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
    uint8_t format_valid;
} byter_platform_framebuffer_info;

struct byter_platform_boot_info {
    uint32_t dtb_size;
    uint32_t memory_count;
    uint32_t reserved_count;
    byter_platform_range memory[BYTER_PLATFORM_MAX_RANGES];
    byter_platform_range reserved[BYTER_PLATFORM_MAX_RANGES];
    byter_platform_framebuffer_info framebuffer;
};

/* Implemented by the selected platform. */
int byter_platform_get_boot_info(uint8_t *dtb, struct byter_platform_boot_info *info);
int byter_platform_framebuffer_fallback(struct byter_framebuffer *framebuffer);
const char *byter_platform_name(void);

#define BYTER_PLATFORM_BUTTON_VOLUME_UP   (1U << 0)
#define BYTER_PLATFORM_BUTTON_VOLUME_DOWN (1U << 1)
#define BYTER_PLATFORM_BUTTON_POWER       (1U << 2)
#define BYTER_PLATFORM_BUTTON_ESSENTIAL   (1U << 3)

int byter_platform_buttons_init(void);
int byter_platform_buttons_read(unsigned *pressed);
int byter_platform_reboot_fastboot(void);

#endif
