#ifndef BYTER_AARCH64_HANDOFF_H
#define BYTER_AARCH64_HANDOFF_H

#ifdef __ASSEMBLER__
#define BYTER_HANDOFF_ENTRY_OFFSET 0
#define BYTER_HANDOFF_TTBR0_OFFSET 8
#define BYTER_HANDOFF_TTBR1_OFFSET 16
#define BYTER_HANDOFF_TCR_OFFSET 24
#else
#include <stdint.h>

struct byter_aarch64_handoff_context {
    uint64_t entry;
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
};

_Static_assert(sizeof(struct byter_aarch64_handoff_context) == 32,
               "AArch64 handoff context layout changed");
#endif

#endif
