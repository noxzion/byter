#include "../platform.h"
#include "../fdt.h"
#include "buttons.h"
#include "framebuffer.h"

int byter_platform_get_boot_info(uint8_t *dtb, struct byter_platform_boot_info *info) {
    return byter_platform_fdt_read_boot_info(dtb, info);
}

int byter_platform_framebuffer_fallback(struct byter_framebuffer *framebuffer) {
    (void)framebuffer;
    return 0;
}

const char *byter_platform_name(void) { return "QEMU virt"; }
int byter_platform_buttons_init(void) { return 0; }
int byter_platform_buttons_read(unsigned *pressed) { (void)pressed; return 0; }
int byter_platform_reboot_fastboot(void) { return 0; }
