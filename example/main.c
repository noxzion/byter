#include "byter.h"
#include "fdt_diag.h"

#define BYTER_REQUEST __attribute__((used, section(".byter_requests"), aligned(8)))

static volatile uint64_t base_revision[3] BYTER_REQUEST = BYTER_BASE_REVISION(0);
extern void byter_vectors(void);
extern void byter_test_svc(void);
volatile uint64_t byter_exception_count;
volatile uint64_t byter_exception_esr;

static volatile uint64_t request_start[4]
    __attribute__((used, section(".byter_requests_start"), aligned(8))) = BYTER_REQUESTS_START_MARKER;
static volatile uint64_t request_end[2]
    __attribute__((used, section(".byter_requests_end"), aligned(8))) = BYTER_REQUESTS_END_MARKER;

static volatile struct byter_framebuffer_request framebuffer_request BYTER_REQUEST = {
    .id = BYTER_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

static volatile struct byter_memmap_request memmap_request BYTER_REQUEST = {
    .id = BYTER_MEMMAP_REQUEST_ID,
    .revision = 0,
};

static volatile struct byter_dtb_request dtb_request BYTER_REQUEST = {
    .id = BYTER_DTB_REQUEST_ID,
    .revision = 0,
};

static volatile struct byter_hhdm_request hhdm_request BYTER_REQUEST = {
    .id = BYTER_HHDM_REQUEST_ID,
    .revision = 0,
};

static volatile struct byter_executable_address_request executable_address_request BYTER_REQUEST = {
    .id = BYTER_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

static void wait_forever(void) {
    for (;;) __asm__ volatile("wfe");
}

static void install_vectors(void) {
    uintptr_t address = (uintptr_t)byter_vectors;
    __asm__ volatile("msr VBAR_EL1, %0; isb" :: "r"(address) : "memory");
}

static int valid_memory_map(const struct byter_memmap_response *map) {
    if (!map || !map->entries || !map->entry_count || map->entry_count > 512) return 0;

    int has_usable = 0;
    int has_bootloader = 0;
    int has_kernel = 0;
    uint64_t previous_end = 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct byter_memmap_entry *entry = map->entries[i];
        if (!entry || !entry->length || entry->base > UINT64_MAX - entry->length ||
            entry->base < previous_end || entry->type > BYTER_MEMMAP_RESERVED_MAPPED)
            return 0;

        previous_end = entry->base + entry->length;
        if (entry->type == BYTER_MEMMAP_USABLE) has_usable = 1;
        if (entry->type == BYTER_MEMMAP_BOOTLOADER_RECLAIMABLE) has_bootloader = 1;
        if (entry->type == BYTER_MEMMAP_EXECUTABLE_AND_MODULES) has_kernel = 1;
    }
    return has_usable && has_bootloader && has_kernel;
}

static int valid_executable_address(const struct byter_executable_address_response *address,
                                   const struct byter_memmap_response *map) {
    if (!address || address->revision != 0 || !address->physical_base ||
        address->virtual_base < 0xffffffff80000000ULL || !map || !map->entries)
        return 0;

    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct byter_memmap_entry *entry = map->entries[i];
        if (entry->type == BYTER_MEMMAP_EXECUTABLE_AND_MODULES &&
            address->physical_base >= entry->base &&
            address->physical_base - entry->base < entry->length)
            return 1;
    }
    return 0;
}

static int valid_hhdm(const struct byter_hhdm_response *hhdm) {
    return hhdm && hhdm->revision == 0 &&
           hhdm->offset >= 0xffff000000000000ULL &&
           !(hhdm->offset & 0x1fffff);
}

static int framebuffer_is_hhdm(const struct byter_framebuffer *fb,
                               const struct byter_hhdm_response *hhdm,
                               const struct byter_memmap_response *map) {
    if (!fb || !fb->address || !valid_hhdm(hhdm) ||
        (uint64_t)fb->address < hhdm->offset || !map || !map->entries ||
        !fb->pitch || !fb->height || fb->height > UINT64_MAX / fb->pitch)
        return 0;

    uint64_t physical = (uint64_t)fb->address - hhdm->offset;
    uint64_t size = fb->pitch * fb->height;
    if (physical > UINT64_MAX - size) return 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct byter_memmap_entry *entry = map->entries[i];
        if (entry->type == BYTER_MEMMAP_FRAMEBUFFER && physical >= entry->base &&
            physical - entry->base <= entry->length &&
            size <= entry->length - (physical - entry->base))
            return 1;
    }
    return 0;
}

static uint32_t rgb(const struct byter_framebuffer *fb, uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << fb->red_mask_shift) |
           ((uint32_t)green << fb->green_mask_shift) |
           ((uint32_t)blue << fb->blue_mask_shift);
}

static void show_stage(const struct byter_framebuffer *fb,
                       uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t side = fb->width < fb->height ? (uint32_t)fb->width : (uint32_t)fb->height;
    if (side > 160) side = 160;
    uint32_t left = ((uint32_t)fb->width - side) / 2;
    uint32_t top = ((uint32_t)fb->height - side) / 2;
    volatile uint32_t *pixels = fb->address;
    uint32_t stride = (uint32_t)(fb->pitch / 4);
    uint32_t color = rgb(fb, red, green, blue);
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x)
            pixels[(top + y) * stride + left + x] = color;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void show_diagnostic_marker(const struct byter_framebuffer *fb,
                                   uint8_t red, uint8_t green, uint8_t blue) {
    volatile uint32_t *pixels = fb->address;
    uint32_t stride = (uint32_t)(fb->pitch / 4);
    uint32_t color = rgb(fb, red, green, blue);
    uint32_t side = fb->width < 32 ? (uint32_t)fb->width : 32;
    if (fb->height < side) side = (uint32_t)fb->height;
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x)
            pixels[y * stride + x] = color;
    __asm__ volatile("dsb sy" ::: "memory");
}

static int valid_framebuffer_shape(const struct byter_framebuffer *fb) {
    return fb && fb->address && fb->memory_model == BYTER_FRAMEBUFFER_RGB && fb->bpp == 32 &&
        fb->width >= 32 && fb->height >= 32 && fb->width <= 16384 && fb->height <= 16384 &&
        fb->pitch >= fb->width * 4 && fb->pitch % 4 == 0 &&
        fb->height <= UINT64_MAX / fb->pitch &&
        fb->red_mask_size == 8 && fb->green_mask_size == 8 && fb->blue_mask_size == 8 &&
        fb->red_mask_shift <= 24 && fb->green_mask_shift <= 24 && fb->blue_mask_shift <= 24;
}

void _start(void) {
    install_vectors();

    struct byter_hhdm_response *hhdm = hhdm_request.response;
    if (!valid_hhdm(hhdm)) wait_forever();

    struct byter_framebuffer_response *response = framebuffer_request.response;
    if (!response || !response->framebuffer_count || !response->framebuffers ||
        !response->framebuffers[0]) wait_forever();

    struct byter_framebuffer *fb = response->framebuffers[0];
    if (!valid_framebuffer_shape(fb) || (uint64_t)fb->address < hhdm->offset) wait_forever();
    show_stage(fb, 0x3b, 0x82, 0xf6); /* Kernel entered. */

    if (!BYTER_LOADED_BASE_REVISION_VALID(base_revision) ||
        !BYTER_BASE_REVISION_SUPPORTED(base_revision) ||
        BYTER_LOADED_BASE_REVISION(base_revision) != 0) wait_forever();
    show_stage(fb, 0x8b, 0x5c, 0xf6); /* Base revision 0 accepted. */

    struct byter_memmap_response *memmap = memmap_request.response;
    if (!valid_memory_map(memmap)) wait_forever();
    show_stage(fb, 0xec, 0x48, 0x99); /* Memory map accepted. */

    if (!valid_executable_address(executable_address_request.response, memmap))
        wait_forever();
    show_stage(fb, 0xf5, 0x9e, 0x0b); /* Executable mapping accepted. */

    if (!framebuffer_is_hhdm(fb, hhdm, memmap)) wait_forever();
    show_stage(fb, 0x06, 0xb6, 0xd4); /* Framebuffer HHDM mapping accepted. */

    struct byter_dtb_response *dtb = dtb_request.response;
    if (!dtb || dtb->revision != 0 ||
        !byter_example_fdt_device_map_check(dtb->dtb_ptr, memmap)) {
        show_diagnostic_marker(fb, 0xff, 0x00, 0x00); /* DTB device map check failed. */
        wait_forever();
    }

    show_stage(fb, 0xff, 0x00, 0xff); /* EL1 synchronous exception test armed. */
    byter_test_svc();
    uint64_t exception_class = (byter_exception_esr >> 26) & 0x3f;
    if (byter_exception_count != 1 || exception_class != 0x15) {
        show_stage(fb, 0xff, 0x00, 0x00); /* SVC was not handled and returned. */
        wait_forever();
    }

    volatile uint32_t *pixels = fb->address;
    uint32_t stride = (uint32_t)(fb->pitch / 4);
    for (uint32_t y = 0; y < fb->height; ++y)
        for (uint32_t x = 0; x < fb->width; ++x)
            pixels[y * stride + x] = rgb(fb, 16, 24, 32);

    uint32_t side = fb->width < fb->height ? (uint32_t)fb->width : (uint32_t)fb->height;
    if (side > 160) side = 160;
    uint32_t left = ((uint32_t)fb->width - side) / 2;
    uint32_t top = ((uint32_t)fb->height - side) / 2;
    uint32_t square_color = rgb(fb, 0x22, 0xc5, 0x5e);

    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x)
            pixels[(top + y) * stride + left + x] = square_color;

    show_diagnostic_marker(fb, 0x00, 0xff, 0xff); /* A DTB device reg is RESERVED in the map. */
    __asm__ volatile("dsb sy" ::: "memory");
    wait_forever();
}
