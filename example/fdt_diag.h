#ifndef BYTER_EXAMPLE_FDT_DIAG_H
#define BYTER_EXAMPLE_FDT_DIAG_H

#include "byter.h"

enum byter_example_fdt_map_status {
    BYTER_EXAMPLE_FDT_MAP_INVALID = 0,
    BYTER_EXAMPLE_FDT_MAP_DEVICE_RESERVED,
    BYTER_EXAMPLE_FDT_MAP_HIGH_EXTERNAL_MAPPED,
};

int byter_example_fdt_device_map_check(const void *dtb,
                                      const struct byter_memmap_response *map);

#endif
