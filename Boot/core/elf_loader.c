#include "byter.h"
#include "../platform/a059/framebuffer.h"
#include "../arch/aarch64/page_tables.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define MAX_RANGES 256
#define MAX_OUT_RANGES 512
#define MAX_PHDRS 128
#define MAX_MAP_BOUNDARIES (2 + 2 * (MAX_RANGES + MAX_PHDRS + MAX_OUT_RANGES + 4))
#define MAX_IMAGE_SIZE (64ULL * 1024 * 1024)
#define MAX_ELF_MEMORY (256ULL * 1024 * 1024)
#define HANDOFF_OFFSET 0x100000ULL
#define HANDOFF_SIZE 0x10000ULL
#define STACK_SIZE 0x30000ULL

typedef struct { u64 base, length; } range;
typedef struct { u64 entry, ttbr0, ttbr1, tcr; } boot_context;
typedef struct {
    u64 address, size, width, height, pitch;
    u8 red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
    u8 format_valid;
} framebuffer_info;
typedef struct { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; } elf_program;
typedef struct {
    u8 ident[16]; u16 type, machine; u32 version; u64 entry, phoff, shoff;
    u32 flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_header;
void *memset(void *dst, int value, unsigned long size) {
    u8 *p = (u8 *)dst;
    for (unsigned long i = 0; i < size; ++i) p[i] = (u8)value;
    return dst;
}

static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u64 cells_value(const u8 *p, u32 cells) {
    u64 value = 0;
    for (u32 i = 0; i < cells; ++i) value = (value << 32) | be32(p + i * 4);
    return value;
}

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int valid_string(const char *table, u32 size, u32 offset) {
    if (offset >= size) return 0;
    for (u32 i = offset; i < size; ++i) if (!table[i]) return 1;
    return 0;
}

static int begins_memory(const u8 *name) {
    return name[0] == 'm' && name[1] == 'e' && name[2] == 'm' && name[3] == 'o' &&
        name[4] == 'r' && name[5] == 'y' && (name[6] == 0 || name[6] == '@');
}

static int begins_framebuffer(const u8 *name) {
    static const char prefix[] = "framebuffer";
    for (u32 i = 0; i < sizeof(prefix) - 1; ++i)
        if (name[i] != (u8)prefix[i]) return 0;
    return name[sizeof(prefix) - 1] == 0 || name[sizeof(prefix) - 1] == '@';
}

static int string_list_has(const u8 *strings, u32 length, const char *wanted) {
    for (u32 offset = 0; offset < length;) {
        u32 end = offset;
        while (end < length && strings[end]) ++end;
        if (end == length) return 0;
        u32 i = 0;
        while (offset + i < end && wanted[i] && strings[offset + i] == (u8)wanted[i]) ++i;
        if (offset + i == end && wanted[i] == 0) return 1;
        offset = end + 1;
    }
    return 0;
}

static u32 property_u32(const u8 *data, u32 length) {
    return length == 4 ? be32(data) : 0;
}

static int parse_framebuffer_format(framebuffer_info *fb, const u8 *data, u32 length) {
    if (length != 9 || data[length - 1] != 0) return 0;
    if (data[0] == 'a' && data[1] == '8' && data[2] == 'r' && data[3] == '8' &&
        data[4] == 'g' && data[5] == '8' && data[6] == 'b' && data[7] == '8' && data[8] == 0) {
        fb->red_size = 8; fb->red_shift = 16;
        fb->green_size = 8; fb->green_shift = 8;
        fb->blue_size = 8; fb->blue_shift = 0;
        fb->format_valid = 1;
        return 1;
    }
    if (data[0] == 'a' && data[1] == '8' && data[2] == 'b' && data[3] == '8' &&
        data[4] == 'g' && data[5] == '8' && data[6] == 'r' && data[7] == '8' && data[8] == 0) {
        fb->red_size = 8; fb->red_shift = 0;
        fb->green_size = 8; fb->green_shift = 8;
        fb->blue_size = 8; fb->blue_shift = 16;
        fb->format_valid = 1;
        return 1;
    }
    return 0;
}

static int add_regs(range *out, u32 *count, const u8 *data, u32 length, u32 ac, u32 sc) {
    if (ac == 0 || ac > 2 || sc == 0 || sc > 2 || length % ((ac + sc) * 4)) return 0;
    for (u32 off = 0; off < length; off += (ac + sc) * 4) {
        u64 base = cells_value(data + off, ac);
        u64 size = cells_value(data + off + ac * 4, sc);
        if (!size) continue;
        if (base > ~0ULL - size || *count == MAX_RANGES) return 0;
        out[(*count)++] = (range){base, size};
    }
    return 1;
}

/* Parse only the FDT memory and reserved-memory ranges needed for safe PT_LOAD placement. */
static int parse_fdt(u8 *dtb, u32 *total_size, range *memory, u32 *memory_count,
                     range *reserved, u32 *reserved_count, framebuffer_info *framebuffer) {
    if (!dtb || be32(dtb) != 0xd00dfeed) return 0;
    u32 total = be32(dtb + 4), struct_off = be32(dtb + 8), strings_off = be32(dtb + 12);
    u32 strings_size = be32(dtb + 32), struct_size = be32(dtb + 36);
    if (total < 40 || total > 16 * 1024 * 1024 || struct_off > total || struct_size > total - struct_off ||
        strings_off > total || strings_size > total - strings_off) return 0;
    u8 *st = dtb + struct_off, *st_end = st + struct_size;
    const char *strings = (const char *)(dtb + strings_off);
    u32 ac = 2, sc = 2, depth = 0;
    int ended = 0;

    /* Root cell widths are needed before parsing any child reg properties. */
    for (u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
        } else if (token == 2) {
            if (!depth) return 0;
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth == 1) {
                if (equal(name, "#address-cells")) { if (length != 4) return 0; ac = be32(p); }
                if (equal(name, "#size-cells")) { if (length != 4) return 0; sc = be32(p); }
            }
            p = (u8 *)(((u64)(p + length) + 3) & ~3ULL);
        } else if (token == 4) {
        } else if (token == 9) { ended = 1; break; }
        else return 0;
    }
    if (!ended || depth || ac == 0 || ac > 2 || sc == 0 || sc > 2) return 0;

    u8 memory_node[32] = {0}, reserved_parent[32] = {0}, reserved_node[32] = {0};
    u8 chosen_node[32] = {0};
    u8 framebuffer_node[32] = {0}, framebuffer_compatible[32] = {0};
    framebuffer_info node_framebuffer[32] = {{0}};
    u32 node_ac[32] = {0}, node_sc[32] = {0};
    node_ac[0] = ac; node_sc[0] = sc;
    depth = 0; ended = 0; *memory_count = 0; *reserved_count = 0;
    *framebuffer = (framebuffer_info){0};
    for (u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            if (depth + 1 >= 32) return 0;
            u8 *name = p;
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
            node_ac[depth] = depth == 1 ? ac : node_ac[depth - 1];
            node_sc[depth] = depth == 1 ? sc : node_sc[depth - 1];
            memory_node[depth] = depth == 2 && begins_memory(name);
            reserved_parent[depth] = depth == 2 && equal((const char *)name, "reserved-memory");
            reserved_node[depth] = depth == 3 && reserved_parent[depth - 1];
            chosen_node[depth] = depth == 2 && equal((const char *)name, "chosen");
            framebuffer_node[depth] = depth == 3 && chosen_node[depth - 1] && begins_framebuffer(name);
        } else if (token == 2) {
            if (!depth) return 0;
            if (framebuffer_node[depth] && framebuffer_compatible[depth] &&
                !framebuffer->format_valid) {
                framebuffer_info *candidate = &node_framebuffer[depth];
                if (candidate->address && candidate->size && candidate->width && candidate->height &&
                    candidate->pitch && candidate->format_valid &&
                    candidate->width <= 16384 && candidate->height <= 16384 &&
                    candidate->pitch >= candidate->width * 4 &&
                    candidate->pitch % 4 == 0 &&
                    candidate->height <= ~0ULL / candidate->pitch &&
                    candidate->size >= candidate->pitch * candidate->height &&
                    candidate->address <= ~0ULL - candidate->size)
                    *framebuffer = *candidate;
            }
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth && equal(name, "#address-cells")) {
                if (length != 4) return 0;
                node_ac[depth] = be32(p);
            }
            if (depth && equal(name, "#size-cells")) {
                if (length != 4) return 0;
                node_sc[depth] = be32(p);
            }
            if (depth && framebuffer_node[depth]) {
                framebuffer_info *candidate = &node_framebuffer[depth];
                if (equal(name, "compatible"))
                    framebuffer_compatible[depth] = string_list_has(p, length, "simple-framebuffer");
                else if (equal(name, "reg")) {
                    u32 ac = node_ac[depth - 1], sc = node_sc[depth - 1];
                    if (ac && ac <= 2 && sc && sc <= 2 && length == (ac + sc) * 4) {
                        candidate->address = cells_value(p, ac);
                        candidate->size = cells_value(p + ac * 4, sc);
                    }
                } else if (equal(name, "width")) candidate->width = property_u32(p, length);
                else if (equal(name, "height")) candidate->height = property_u32(p, length);
                else if (equal(name, "stride")) candidate->pitch = property_u32(p, length);
                else if (equal(name, "format")) parse_framebuffer_format(candidate, p, length);
            }
            if (depth && equal(name, "reg")) {
                if (memory_node[depth] && !add_regs(memory, memory_count, p, length, node_ac[depth - 1], node_sc[depth - 1])) return 0;
                if (reserved_node[depth]) {
                    if (!add_regs(reserved, reserved_count, p, length, node_ac[depth - 1], node_sc[depth - 1])) return 0;
                }
            }
            p = (u8 *)(((u64)(p + length) + 3) & ~3ULL);
            if (p > st_end) return 0;
        } else if (token == 4) {
        } else if (token == 9) { ended = 1; break; }
        else return 0;
    }
    *total_size = total;
    return ended && depth == 0 && *memory_count != 0;
}

static int subtract_range(range *ranges, u32 *count, range cut) {
    if (!cut.length || cut.base > ~0ULL - cut.length) return 0;
    u64 cut_end = cut.base + cut.length;
    for (u32 i = 0; i < *count;) {
        range r = ranges[i];
        u64 end = r.base + r.length;
        if (cut.base >= end || cut_end <= r.base) { ++i; continue; }
        int left = cut.base > r.base, right = cut_end < end;
        if (left && right) {
            if (*count == MAX_OUT_RANGES) return 0;
            for (u32 j = *count; j > i + 1; --j) ranges[j] = ranges[j - 1];
            ranges[i] = (range){r.base, cut.base - r.base};
            ranges[i + 1] = (range){cut_end, end - cut_end};
            ++*count; i += 2;
        } else if (left) {
            ranges[i].length = cut.base - r.base; ++i;
        } else if (right) {
            ranges[i] = (range){cut_end, end - cut_end}; ++i;
        } else {
            for (u32 j = i + 1; j < *count; ++j) ranges[j - 1] = ranges[j];
            --*count;
        }
    }
    return 1;
}

static int in_memory(range *memory, u32 count, range candidate) {
    if (!candidate.length || candidate.base > ~0ULL - candidate.length) return 0;
    u64 end = candidate.base + candidate.length;
    for (u32 i = 0; i < count; ++i)
        if (candidate.base >= memory[i].base && end <= memory[i].base + memory[i].length) return 1;
    return 0;
}

static int overlaps(range a, range b) {
    return a.base < b.base + b.length && b.base < a.base + a.length;
}

static int page_aligned_range(range input, range *output) {
    if (!input.length || input.base > ~0ULL - input.length) return 0;
    u64 end = input.base + input.length;
    if (end > ~0ULL - 0xfff) return 0;
    output->base = input.base & ~0xfffULL;
    u64 aligned_end = (end + 0xfff) & ~0xfffULL;
    output->length = aligned_end - output->base;
    return output->length != 0;
}

static int normalize_memory(range *memory, u32 *count) {
    for (u32 i = 1; i < *count; ++i) {
        range current = memory[i];
        u32 j = i;
        while (j && memory[j - 1].base > current.base) {
            memory[j] = memory[j - 1];
            --j;
        }
        memory[j] = current;
    }
    u32 normalized = 0;
    for (u32 i = 0; i < *count; ++i) {
        range current = memory[i];
        if (!current.length || current.base > ~0ULL - current.length) return 0;
        if (normalized) {
            range *previous = &memory[normalized - 1];
            u64 previous_end = previous->base + previous->length;
            if (current.base < previous_end) return 0;
            if (current.base == previous_end) {
                previous->length += current.length;
                continue;
            }
        }
        memory[normalized++] = current;
    }
    *count = normalized;
    return normalized != 0;
}

static int contains_range(range region, range candidate) {
    return candidate.base >= region.base && candidate.base <= ~0ULL - candidate.length &&
           region.base <= ~0ULL - region.length &&
           candidate.base + candidate.length <= region.base + region.length;
}

static int add_map_boundaries(u64 *boundaries, u32 *count, range memory, range cut) {
    if (!cut.length || cut.base > ~0ULL - cut.length || !overlaps(memory, cut)) return 1;
    u64 start = cut.base > memory.base ? cut.base : memory.base;
    u64 memory_end = memory.base + memory.length;
    u64 cut_end = cut.base + cut.length;
    u64 end = cut_end < memory_end ? cut_end : memory_end;
    if (start > memory.base) boundaries[(*count)++] = start;
    if (end < memory_end) boundaries[(*count)++] = end;
    return *count <= MAX_MAP_BOUNDARIES;
}

static void sort_boundaries(u64 *boundaries, u32 count) {
    for (u32 i = 1; i < count; ++i) {
        u64 current = boundaries[i];
        u32 j = i;
        while (j && boundaries[j - 1] > current) {
            boundaries[j] = boundaries[j - 1];
            --j;
        }
        boundaries[j] = current;
    }
}

static int append_map_entry(struct byter_memmap_entry *entries, u32 *count,
                            range region, u64 type) {
    if (!region.length) return 1;
    if (*count && entries[*count - 1].type == type &&
        entries[*count - 1].base + entries[*count - 1].length == region.base) {
        entries[*count - 1].length += region.length;
        return 1;
    }
    if (*count == MAX_OUT_RANGES) return 0;
    entries[(*count)++] = (struct byter_memmap_entry){region.base, region.length, type};
    return 1;
}

static int build_memory_map(range *memory, u32 memory_count,
                            range *reserved, u32 reserved_count,
                            range shim, range dtb, range *segments, u32 segment_count,
                            range framebuffer, int has_framebuffer,
                            range *usable, u32 usable_count,
                            struct byter_memmap_entry *entries, u32 *entry_count) {
    *entry_count = 0;
    for (u32 m = 0; m < memory_count; ++m) {
        range ram = memory[m];
        u64 boundaries[MAX_MAP_BOUNDARIES];
        u32 boundary_count = 2;
        boundaries[0] = ram.base;
        boundaries[1] = ram.base + ram.length;
        for (u32 i = 0; i < reserved_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, reserved[i])) return 0;
        if (!add_map_boundaries(boundaries, &boundary_count, ram, shim) ||
            !add_map_boundaries(boundaries, &boundary_count, ram, dtb)) return 0;
        for (u32 i = 0; i < segment_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, segments[i])) return 0;
        if (has_framebuffer && !add_map_boundaries(boundaries, &boundary_count, ram, framebuffer)) return 0;
        for (u32 i = 0; i < usable_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, usable[i])) return 0;

        sort_boundaries(boundaries, boundary_count);
        u32 unique_count = 0;
        for (u32 i = 0; i < boundary_count; ++i)
            if (!unique_count || boundaries[i] != boundaries[unique_count - 1])
                boundaries[unique_count++] = boundaries[i];

        for (u32 i = 0; i + 1 < unique_count; ++i) {
            range piece = {boundaries[i], boundaries[i + 1] - boundaries[i]};
            if (!piece.length) continue;
            u64 type = BYTER_MEMMAP_RESERVED;
            for (u32 j = 0; j < usable_count; ++j)
                if (contains_range(usable[j], piece)) { type = BYTER_MEMMAP_USABLE; break; }
            for (u32 j = 0; j < reserved_count; ++j)
                if (contains_range(reserved[j], piece)) { type = BYTER_MEMMAP_RESERVED; break; }
            if (contains_range(shim, piece) || contains_range(dtb, piece))
                type = BYTER_MEMMAP_BOOTLOADER_RECLAIMABLE;
            for (u32 j = 0; j < segment_count; ++j)
                if (contains_range(segments[j], piece)) { type = BYTER_MEMMAP_EXECUTABLE_AND_MODULES; break; }
            if (has_framebuffer && contains_range(framebuffer, piece))
                type = BYTER_MEMMAP_FRAMEBUFFER;
            if (!append_map_entry(entries, entry_count, piece, type)) return 0;
        }
    }
    return *entry_count != 0;
}

static void copy_bytes(u8 *dst, const u8 *src, u64 length) {
    for (u64 i = 0; i < length; ++i) dst[i] = src[i];
}

static void zero_bytes(u8 *dst, u64 length) {
    for (u64 i = 0; i < length; ++i) dst[i] = 0;
}

static void flush_loaded_segment(range r, int executable) {
    u64 start = r.base & ~63ULL, end = r.base + r.length;
    for (u64 p = start; p < end; p += 64) __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    if (executable) {
        for (u64 p = start; p < end; p += 64) __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory");
        __asm__ volatile("dsb sy; isb" ::: "memory");
    }
}

static int valid_elf(const u8 *file, u64 size, elf_header **header, elf_program **programs) {
    if (size < sizeof(elf_header)) return 0;
    elf_header *h = (elf_header *)file;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' || h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 2 || h->ident[5] != 1 || h->ident[6] != 1 || h->type != 2 || h->machine != 183 ||
        h->version != 1 || h->ehsize != sizeof(elf_header) || h->phentsize != sizeof(elf_program) ||
        h->phnum == 0 || h->phnum > MAX_PHDRS || h->phoff > size ||
        (u64)h->phnum * sizeof(elf_program) > size - h->phoff) return 0;
    *header = h; *programs = (elf_program *)(file + h->phoff);
    return 1;
}

static const char byter_name[] = "Byter AArch64 shim";
static const char byter_version[] = "0.1";

static int has_id(const u64 *id, const u64 expected[4]) {
    return id[0] == expected[0] && id[1] == expected[1] &&
           id[2] == expected[2] && id[3] == expected[3];
}

static int matches_words(u64 address, const u64 *words, u32 count, u64 end) {
    if (address > end || (u64)count * sizeof(u64) > end - address) return 0;
    const u64 *candidate = (const u64 *)(u64)address;
    for (u32 i = 0; i < count; ++i)
        if (candidate[i] != words[i]) return 0;
    return 1;
}

static void find_request_window(range *segments, u32 count, u64 *window_start,
                                u64 *window_end) {
    const u64 start_marker[4] = BYTER_REQUESTS_START_MARKER;
    const u64 end_marker[2] = BYTER_REQUESTS_END_MARKER;
    u64 image_start = ~0ULL, image_end = 0;
    u64 last_start_marker = 0, first_end_marker = ~0ULL;
    int found_start_marker = 0, found_end_marker = 0;

    for (u32 i = 0; i < count; ++i) {
        range segment = segments[i];
        if (!segment.length || segment.base > ~0ULL - segment.length) continue;
        u64 end = segment.base + segment.length;
        if (segment.base < image_start) image_start = segment.base;
        if (end > image_end) image_end = end;

        if (segment.base > ~0ULL - 7) continue;
        u64 address = (segment.base + 7) & ~7ULL;
        while (address <= end && end - address >= sizeof(end_marker)) {
            if (matches_words(address, start_marker, 4, end)) {
                last_start_marker = address;
                found_start_marker = 1;
            }
            if (matches_words(address, end_marker, 2, end) &&
                (!found_end_marker || address < first_end_marker)) {
                first_end_marker = address;
                found_end_marker = 1;
            }
            if (end - address < sizeof(u64)) break;
            address += sizeof(u64);
        }
    }

    *window_start = found_start_marker ? last_start_marker + sizeof(start_marker) : image_start;
    *window_end = found_end_marker ? first_end_marker : image_end;
    /* Base revision 0 treats delimiters as hints, so ignore malformed bounds. */
    if (*window_start >= *window_end) {
        *window_start = image_start;
        *window_end = image_end;
    }
}

static void resolve_requests(range *segments, u32 count,
                             struct byter_bootloader_info_response *bootloader_info,
                             struct byter_dtb_response *dtb,
                             struct byter_framebuffer_response *framebuffer,
                             struct byter_memmap_response *memmap,
                             struct byter_executable_address_response *executable_address) {
    u64 window_start, window_end;
    find_request_window(segments, count, &window_start, &window_end);
    if (window_start >= window_end) return;

    const u64 common_magic[2] = {0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL};
    const u64 request_size = sizeof(struct byter_memmap_request);
    for (u32 i = 0; i < count; ++i) {
        range segment = segments[i];
        if (!segment.length || segment.base > ~0ULL - segment.length) continue;
        u64 segment_end = segment.base + segment.length;
        u64 start = segment.base > window_start ? segment.base : window_start;
        u64 end = segment_end < window_end ? segment_end : window_end;
        if (start > ~0ULL - 7) continue;
        u64 address = (start + 7) & ~7ULL;
        while (address <= end && end - address >= request_size) {
            u64 *id = (u64 *)(u64)address;
            if (id[0] != common_magic[0] || id[1] != common_magic[1]) {
                if (end - address < sizeof(u64)) break;
                address += sizeof(u64);
                continue;
            }

            u64 *revision = id + 4;
            u64 *response = id + 5;
            *response = 0;
            if (*revision == 0) {
                if (has_id(id, (const u64[])BYTER_BOOTLOADER_INFO_REQUEST_ID))
                    *response = (u64)bootloader_info;
                else if (has_id(id, (const u64[])BYTER_DTB_REQUEST_ID))
                    *response = (u64)dtb;
                else if (has_id(id, (const u64[])BYTER_FRAMEBUFFER_REQUEST_ID))
                    *response = (u64)framebuffer;
                else if (has_id(id, (const u64[])BYTER_MEMMAP_REQUEST_ID))
                    *response = (u64)memmap;
                else if (has_id(id, (const u64[])BYTER_EXECUTABLE_ADDRESS_REQUEST_ID))
                    *response = (u64)executable_address;
            }
            if (end - address < sizeof(u64)) break;
            address += sizeof(u64);
        }
    }
}

/* Returns the ELF entry address, or zero on any validation/loading failure. */
u64 byter_shim_main(u8 *shim, u8 *dtb, u8 *elf, u64 elf_size, u64 shim_size, u64 file_size) {
    u32 dtb_size = 0, memory_count = 0, reserved_count = 0;
    framebuffer_info framebuffer_data = {0};
    range memory[MAX_RANGES], reserved[MAX_RANGES];
    if ((u64)shim > ~0ULL - shim_size || file_size > shim_size) return 0;
    int fdt_valid = parse_fdt(dtb, &dtb_size, memory, &memory_count, reserved,
                              &reserved_count, &framebuffer_data);
    if (!fdt_valid || (u64)dtb > ~0ULL - dtb_size) return 0;
    if (!normalize_memory(memory, &memory_count)) return 0;
    if (!framebuffer_data.format_valid) {
        struct byter_framebuffer fallback;
        if (byter_a059_framebuffer_fallback(&fallback)) {
            framebuffer_data = (framebuffer_info){
                .address = (u64)fallback.address,
                .size = fallback.pitch * fallback.height,
                .width = fallback.width,
                .height = fallback.height,
                .pitch = fallback.pitch,
                .red_size = fallback.red_mask_size,
                .red_shift = fallback.red_mask_shift,
                .green_size = fallback.green_mask_size,
                .green_shift = fallback.green_mask_shift,
                .blue_size = fallback.blue_mask_size,
                .blue_shift = fallback.blue_mask_shift,
                .format_valid = 1,
            };
        }
    }
    if ((u64)elf < (u64)shim || (u64)elf - (u64)shim > file_size ||
        elf_size > file_size - ((u64)elf - (u64)shim)) return 0;

    elf_header *eh;
    elf_program *ph;
    if (!valid_elf(elf, elf_size, &eh, &ph)) return 0;
    range segments[MAX_PHDRS], virtual_segments[MAX_PHDRS];
    range physical_page_ranges[MAX_PHDRS], virtual_page_ranges[MAX_PHDRS];
    struct byter_aarch64_load_segment page_table_segments[MAX_PHDRS];
    u32 segment_count = 0;
    u64 low = ~0ULL, high = 0, virtual_low = ~0ULL, total_mem = 0;
    u64 load_bias = 0;
    int entry_ok = 0;
    for (u32 i = 0; i < eh->phnum; ++i) {
        elf_program *p = &ph[i];
        if (p->type != 1 || !p->memsz) continue;
        if (p->filesz > p->memsz || p->offset > elf_size || p->filesz > elf_size - p->offset ||
            p->paddr > ~0ULL - p->memsz || p->vaddr < p->paddr || p->vaddr > ~0ULL - p->memsz ||
            p->memsz > MAX_ELF_MEMORY - total_mem ||
            (p->align > 1 && ((p->align & (p->align - 1)) || ((p->vaddr - p->offset) & (p->align - 1))))) return 0;
        range r = {p->paddr, p->memsz}, vr = {p->vaddr, p->memsz};
        range physical_page, virtual_page;
        if ((p->paddr & 0xfff) != (p->vaddr & 0xfff) ||
            !page_aligned_range(r, &physical_page) || !page_aligned_range(vr, &virtual_page) ||
            p->vaddr < 0xffff000000000000ULL || !in_memory(memory, memory_count, r) ||
            !in_memory(memory, memory_count, (range){(u64)shim, shim_size}) ||
            !in_memory(memory, memory_count, (range){(u64)dtb, dtb_size}) ||
            overlaps(r, (range){(u64)shim, shim_size}) ||
            overlaps(r, (range){(u64)dtb, dtb_size})) return 0;
        u64 segment_bias = p->vaddr - p->paddr;
        if (segment_count && segment_bias != load_bias) return 0;
        load_bias = segment_bias;
        for (u32 j = 0; j < reserved_count; ++j) if (overlaps(r, reserved[j])) return 0;
        for (u32 j = 0; j < segment_count; ++j)
            if (overlaps(physical_page, physical_page_ranges[j]) ||
                overlaps(virtual_page, virtual_page_ranges[j])) return 0;
        segments[segment_count] = r;
        virtual_segments[segment_count] = vr;
        physical_page_ranges[segment_count] = physical_page;
        virtual_page_ranges[segment_count] = virtual_page;
        page_table_segments[segment_count] = (struct byter_aarch64_load_segment){
            .physical_base = p->paddr, .virtual_base = p->vaddr,
            .length = p->memsz, .flags = p->flags
        };
        ++segment_count;
        total_mem += p->memsz;
        if (r.base < low) low = r.base;
        if (r.base + r.length > high) high = r.base + r.length;
        if (vr.base < virtual_low) virtual_low = vr.base;
        if ((p->flags & 1) && eh->entry >= vr.base && eh->entry - vr.base < vr.length) entry_ok = 1;
    }
    if (!segment_count || !entry_ok || high <= low) return 0;

    range framebuffer_range = {framebuffer_data.address, framebuffer_data.size};
    int has_framebuffer = framebuffer_data.format_valid && framebuffer_range.length &&
        framebuffer_range.base <= ~0ULL - framebuffer_range.length;
    if (has_framebuffer && (overlaps(framebuffer_range, (range){(u64)shim, shim_size}) ||
        overlaps(framebuffer_range, (range){(u64)dtb, dtb_size}))) return 0;
    for (u32 i = 0; i < segment_count; ++i)
        if (has_framebuffer && overlaps(framebuffer_range, segments[i])) return 0;

    range usable[MAX_OUT_RANGES]; u32 usable_count = memory_count;
    for (u32 i = 0; i < memory_count; ++i) usable[i] = memory[i];
    for (u32 i = 0; i < reserved_count; ++i) if (!subtract_range(usable, &usable_count, reserved[i])) return 0;
    if (!subtract_range(usable, &usable_count, (range){(u64)shim, shim_size}) ||
        !subtract_range(usable, &usable_count, (range){(u64)dtb, dtb_size})) return 0;
    for (u32 i = 0; i < segment_count; ++i) if (!subtract_range(usable, &usable_count, segments[i])) return 0;
    if (has_framebuffer && !subtract_range(usable, &usable_count, framebuffer_range)) return 0;

    if (shim_size < HANDOFF_OFFSET + HANDOFF_SIZE + STACK_SIZE + 0x10000) return 0;
    u8 *handoff = shim + shim_size - HANDOFF_OFFSET;
    struct byter_bootloader_info_response *bootloader_info = (void *)handoff;
    struct byter_dtb_response *dtb_response = (void *)(bootloader_info + 1);
    struct byter_framebuffer_response *framebuffer_response = (void *)(dtb_response + 1);
    struct byter_memmap_response *memmap_response = (void *)(framebuffer_response + 1);
    struct byter_executable_address_response *executable_address_response = (void *)(memmap_response + 1);
    struct byter_framebuffer *framebuffer = (void *)(executable_address_response + 1);
    struct byter_framebuffer **framebuffers = (void *)(framebuffer + 1);
    struct byter_memmap_entry *memmap_entries = (void *)(framebuffers + 1);
    struct byter_memmap_entry **memmap_entry_pointers = (void *)(memmap_entries + MAX_OUT_RANGES);
    if ((u8 *)(memmap_entry_pointers + MAX_OUT_RANGES) > handoff + HANDOFF_SIZE) return 0;
    u32 memmap_count = 0;
    if (!build_memory_map(memory, memory_count, reserved, reserved_count,
                          (range){(u64)shim, shim_size}, (range){(u64)dtb, dtb_size},
                          segments, segment_count, framebuffer_range, has_framebuffer,
                          usable, usable_count, memmap_entries, &memmap_count)) return 0;
    for (u32 i = 0; i < memmap_count; ++i) memmap_entry_pointers[i] = &memmap_entries[i];
    *memmap_response = (struct byter_memmap_response){
        .revision = 0, .entry_count = memmap_count, .entries = memmap_entry_pointers
    };
    *executable_address_response = (struct byter_executable_address_response){
        .revision = 0, .physical_base = low, .virtual_base = virtual_low
    };
    *bootloader_info = (struct byter_bootloader_info_response){
        .revision = 0, .name = (char *)byter_name, .version = (char *)byter_version
    };
    *dtb_response = (struct byter_dtb_response){.revision = 0, .dtb_ptr = dtb};
    struct byter_framebuffer_response *framebuffer_result = 0;
    if (has_framebuffer) {
        *framebuffer = (struct byter_framebuffer){
            .address = (void *)(u64)framebuffer_data.address,
            .width = framebuffer_data.width, .height = framebuffer_data.height,
            .pitch = framebuffer_data.pitch, .bpp = 32,
            .memory_model = BYTER_FRAMEBUFFER_RGB,
            .red_mask_size = framebuffer_data.red_size,
            .red_mask_shift = framebuffer_data.red_shift,
            .green_mask_size = framebuffer_data.green_size,
            .green_mask_shift = framebuffer_data.green_shift,
            .blue_mask_size = framebuffer_data.blue_size,
            .blue_mask_shift = framebuffer_data.blue_shift,
        };
        framebuffers[0] = framebuffer;
        *framebuffer_response = (struct byter_framebuffer_response){
            .revision = 0, .framebuffer_count = 1, .framebuffers = framebuffers
        };
        framebuffer_result = framebuffer_response;
    }
    for (u32 i = 0; i < eh->phnum; ++i) {
        elf_program *p = &ph[i];
        if (p->type != 1 || !p->memsz) continue;
        u8 *dst = (u8 *)(u64)p->paddr;
        copy_bytes(dst, elf + p->offset, p->filesz);
        zero_bytes(dst + p->filesz, p->memsz - p->filesz);
        flush_loaded_segment((range){p->paddr, p->memsz}, !!(p->flags & 1));
    }

    resolve_requests(segments, segment_count, bootloader_info, dtb_response,
                     framebuffer_result, memmap_response, executable_address_response);
    struct byter_aarch64_range page_table_memory[MAX_RANGES];
    for (u32 i = 0; i < memory_count; ++i)
        page_table_memory[i] = (struct byter_aarch64_range){memory[i].base, memory[i].length};
    struct byter_aarch64_page_tables page_tables;
    if (!byter_aarch64_build_page_tables(shim, shim_size, page_table_memory, memory_count,
            (struct byter_aarch64_range){framebuffer_range.base, framebuffer_range.length},
            has_framebuffer, page_table_segments, segment_count, &page_tables)) return 0;
    boot_context *context = (boot_context *)(handoff + HANDOFF_SIZE - sizeof(*context));
    *context = (boot_context){eh->entry, page_tables.ttbr0, page_tables.ttbr1, page_tables.tcr};
    return (u64)context;
}
