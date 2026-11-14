#include "boot_catalog.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#define CATALOG_MAGIC "BYTERCAT"
#define CATALOG_HEADER_SIZE 24U
#define CATALOG_ENTRY_SIZE 32U
#define MAX_LABEL_SIZE 63U
#define MAX_CMDLINE_SIZE 1024U
#define MAX_ELF_SIZE (64ULL * 1024 * 1024)

static u32 read_le32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u64 read_le64(const u8 *p) {
    return (u64)read_le32(p) | (u64)read_le32(p + 4) << 32;
}

static int bytes_equal(const u8 *a, const char *b, u32 length) {
    for (u32 i = 0; i < length; ++i)
        if (a[i] != (u8)b[i]) return 0;
    return b[length] == 0;
}

int byter_boot_catalog_parse(uint8_t *shim, uint64_t file_size,
                             uint8_t *catalog, uint64_t catalog_size,
                             struct byter_boot_option *options, u32 *option_count) {
    if (!shim || !catalog || !options || !option_count ||
        (u64)shim > ~0ULL - file_size || (u64)catalog < (u64)shim ||
        (u64)catalog - (u64)shim > file_size ||
        catalog_size > file_size - ((u64)catalog - (u64)shim) ||
        catalog_size < CATALOG_HEADER_SIZE) return 0;

    if (!bytes_equal(catalog, CATALOG_MAGIC, 8) || read_le32(catalog + 8) != 1)
        return 0;
    u32 count = read_le32(catalog + 12);
    u32 entry_size = read_le32(catalog + 16);
    u32 total_size = read_le32(catalog + 20);
    if (!count || count > BYTER_BOOT_MAX_OPTIONS || entry_size != CATALOG_ENTRY_SIZE ||
        total_size != catalog_size ||
        count > (catalog_size - CATALOG_HEADER_SIZE) / CATALOG_ENTRY_SIZE) return 0;

    u32 data_start = CATALOG_HEADER_SIZE + count * CATALOG_ENTRY_SIZE;
    u64 catalog_offset = (u64)catalog - (u64)shim;
    u64 catalog_end = catalog_offset + catalog_size;
    u64 elf_start[BYTER_BOOT_MAX_OPTIONS], elf_end[BYTER_BOOT_MAX_OPTIONS];

    for (u32 i = 0; i < count; ++i) {
        const u8 *entry = catalog + CATALOG_HEADER_SIZE + i * CATALOG_ENTRY_SIZE;
        u32 name_offset = read_le32(entry);
        u32 name_length = read_le32(entry + 4);
        u64 elf_offset = read_le64(entry + 8);
        u64 elf_size = read_le64(entry + 16);
        u32 cmdline_offset = read_le32(entry + 24);
        u32 cmdline_size = read_le32(entry + 28);

        if (!name_length || name_length > MAX_LABEL_SIZE || name_offset < data_start ||
            name_offset >= catalog_size || name_length >= catalog_size - name_offset ||
            catalog[name_offset + name_length] != 0 ||
            cmdline_offset < data_start || cmdline_offset >= catalog_size ||
            !cmdline_size || cmdline_size > MAX_CMDLINE_SIZE + 1 ||
            cmdline_size > catalog_size - cmdline_offset ||
            catalog[cmdline_offset + cmdline_size - 1] != 0 ||
            !elf_size || elf_size > MAX_ELF_SIZE || elf_offset < catalog_end ||
            elf_offset > file_size || elf_size > file_size - elf_offset) return 0;

        for (u32 j = 0; j < name_length; ++j)
            if (catalog[name_offset + j] < 0x20 || catalog[name_offset + j] > 0x7e)
                return 0;
        for (u32 j = 0; j + 1 < cmdline_size; ++j)
            if (!catalog[cmdline_offset + j]) return 0;

        for (u32 j = 0; j < i; ++j) {
            if (elf_offset < elf_end[j] && elf_start[j] < elf_offset + elf_size)
                return 0;
            if (options[j].name && bytes_equal(catalog + name_offset,
                                                options[j].name, name_length)) return 0;
        }
        elf_start[i] = elf_offset;
        elf_end[i] = elf_offset + elf_size;
        options[i] = (struct byter_boot_option){
            .name = (const char *)(catalog + name_offset),
            .elf = shim + elf_offset,
            .elf_size = elf_size,
            .cmdline = catalog + cmdline_offset,
            .cmdline_size = cmdline_size,
        };
    }

    *option_count = count;
    return 1;
}
