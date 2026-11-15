#ifndef BYTER_PLATFORM_FDT_H
#define BYTER_PLATFORM_FDT_H

#include "platform.h"

int byter_platform_fdt_read_boot_info(uint8_t *dtb,
                                      struct byter_platform_boot_info *info);

#endif
