#include "../platform.h"
#include "../fdt.h"
#include "buttons.h"
#include "framebuffer.h"

int byter_platform_get_boot_info(uint8_t *dtb, struct byter_platform_boot_info *info) {
    return byter_platform_fdt_read_boot_info(dtb, info);
}

int byter_platform_framebuffer_fallback(struct byter_framebuffer *framebuffer) {
    return byter_a059_framebuffer_fallback(framebuffer);
}

const char *byter_platform_name(void) {
    return "Nothing Phone (3a)";
}

int byter_platform_buttons_init(void) {
    return byter_a059_buttons_init();
}

int byter_platform_buttons_read(unsigned *pressed) {
    return byter_a059_buttons_read(pressed);
}

int byter_platform_reboot_fastboot(void) {
    return byter_a059_reboot_fastboot();
}
