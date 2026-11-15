#include "fdt.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef byter_platform_range range;
typedef byter_platform_framebuffer_info framebuffer_info;

#define MAX_RANGES BYTER_PLATFORM_MAX_RANGES

typedef struct {
    u32 address_cells, size_cells;
    const u8 *ranges;
    u32 ranges_size;
    u8 has_ranges, excluded;
} fdt_node_context;

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
        if (base > ~0ULL - size || *count == BYTER_PLATFORM_MAX_RANGES) return 0;
        out[(*count)++] = (range){base, size};
    }
    return 1;
}

/* Parse FDT RAM, reservations, and framebuffer metadata. */
static int parse_fdt(u8 *dtb, u32 *total_size, range *memory, u32 *memory_count,
                     range *reserved, u32 *reserved_count, framebuffer_info *framebuffer,
                     u32 *root_address_cells, u32 *root_size_cells) {
    if (!dtb || be32(dtb) != 0xd00dfeed) return 0;
    u32 total = be32(dtb + 4), struct_off = be32(dtb + 8), strings_off = be32(dtb + 12);
    u32 reserve_off = be32(dtb + 16);
    u32 strings_size = be32(dtb + 32), struct_size = be32(dtb + 36);
    if (total < 40 || total > 16 * 1024 * 1024 || struct_off > total || struct_size > total - struct_off ||
        strings_off > total || strings_size > total - strings_off || reserve_off < 40 ||
        (reserve_off & 7) || reserve_off > total - 16) return 0;

    *memory_count = 0;
    *reserved_count = 0;
    u32 reserve_cursor = reserve_off;
    for (;;) {
        if (reserve_cursor > total - 16) return 0;
        u64 base = cells_value(dtb + reserve_cursor, 2);
        u64 size = cells_value(dtb + reserve_cursor + 8, 2);
        reserve_cursor += 16;
        if (!base && !size) break;
        if (!size || base > ~0ULL - size || *reserved_count == BYTER_PLATFORM_MAX_RANGES) return 0;
        reserved[(*reserved_count)++] = (range){base, size};
    }

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
    *root_address_cells = ac;
    *root_size_cells = sc;

    u8 memory_node[32] = {0}, reserved_parent[32] = {0}, reserved_node[32] = {0};
    u8 chosen_node[32] = {0};
    u8 framebuffer_node[32] = {0}, framebuffer_compatible[32] = {0};
    framebuffer_info node_framebuffer[32] = {{0}};
    u32 node_ac[32] = {0}, node_sc[32] = {0};
    node_ac[0] = ac; node_sc[0] = sc;
    depth = 0; ended = 0; *memory_count = 0;
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

static int address_fits_cells(u64 address, u64 length, u32 cells) {
    if (!length || address > ~0ULL - (length - 1)) return 0;
    if (cells == 1) return address <= 0xffffffffULL && length - 1 <= 0xffffffffULL - address;
    return cells == 2;
}

static int translate_fdt_range(fdt_node_context *nodes, u32 node_depth,
                               range input, range *output) {
    if (!input.length || input.base > ~0ULL - (input.length - 1)) return 0;
    u64 address = input.base;
    for (u32 bus_depth = node_depth - 1; bus_depth > 1; --bus_depth) {
        fdt_node_context *bus = &nodes[bus_depth];
        u32 parent_cells = nodes[bus_depth - 1].address_cells;
        if (!bus->has_ranges) return 0;
        if (!bus->ranges_size) {
            if (!address_fits_cells(address, input.length, parent_cells)) return 0;
            continue;
        }

        u32 child_cells = bus->address_cells;
        u32 size_cells = bus->size_cells;
        if (!child_cells || child_cells > 2 || !parent_cells || parent_cells > 2 ||
            !size_cells || size_cells > 2) return 0;
        u32 tuple_cells = child_cells + parent_cells + size_cells;
        u32 tuple_size = tuple_cells * 4;
        if (!tuple_size || bus->ranges_size % tuple_size) return 0;

        int mapped = 0;
        for (u32 offset = 0; offset < bus->ranges_size; offset += tuple_size) {
            const u8 *entry = bus->ranges + offset;
            u64 child_base = cells_value(entry, child_cells);
            u64 parent_base = cells_value(entry + child_cells * 4, parent_cells);
            u64 span = cells_value(entry + (child_cells + parent_cells) * 4, size_cells);
            if (!span || child_base > ~0ULL - span || address < child_base) continue;
            u64 delta = address - child_base;
            if (delta > span || input.length > span - delta || parent_base > ~0ULL - delta)
                continue;
            address = parent_base + delta;
            if (!address_fits_cells(address, input.length, parent_cells)) continue;
            mapped = 1;
            break;
        }
        if (!mapped) return 0;
    }
    *output = (range){address, input.length};
    return 1;
}

static int parse_fdt_device_ranges(u8 *dtb, u32 total_size, u32 root_address_cells,
                                   u32 root_size_cells, range *ranges, u32 *range_count) {
    u32 struct_off = be32(dtb + 8), strings_off = be32(dtb + 12);
    u32 strings_size = be32(dtb + 32), struct_size = be32(dtb + 36);
    if (struct_off > total_size || struct_size > total_size - struct_off ||
        strings_off > total_size || strings_size > total_size - strings_off) return 0;
    const u8 *st = dtb + struct_off, *st_end = st + struct_size;
    const char *strings = (const char *)(dtb + strings_off);
    fdt_node_context nodes[32] = {{0}};
    u32 depth = 0;
    int ended = 0;
    for (const u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            if (depth + 1 >= 32) return 0;
            const u8 *name = p;
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (const u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
            nodes[depth] = (fdt_node_context){
                .address_cells = depth == 1 ? root_address_cells : nodes[depth - 1].address_cells,
                .size_cells = depth == 1 ? root_size_cells : nodes[depth - 1].size_cells,
                .excluded = depth > 1 && nodes[depth - 1].excluded,
            };
            if (depth == 2 && (begins_memory(name) ||
                equal((const char *)name, "reserved-memory") ||
                equal((const char *)name, "chosen") || equal((const char *)name, "cpus")))
                nodes[depth].excluded = 1;
        } else if (token == 2) {
            if (!depth) return 0;
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth && equal(name, "#address-cells"))
                nodes[depth].address_cells = length == 4 ? be32(p) : 0;
            else if (depth && equal(name, "#size-cells"))
                nodes[depth].size_cells = length == 4 ? be32(p) : 0;
            else if (depth && equal(name, "ranges")) {
                nodes[depth].has_ranges = 1;
                nodes[depth].ranges = p;
                nodes[depth].ranges_size = length;
            } else if (depth > 1 && !nodes[depth].excluded && equal(name, "reg")) {
                u32 address_cells = nodes[depth - 1].address_cells;
                u32 size_cells = nodes[depth - 1].size_cells;
                if (address_cells && address_cells <= 2 && size_cells && size_cells <= 2) {
                    u32 tuple_size = (address_cells + size_cells) * 4;
                    if (length % tuple_size == 0) {
                        for (u32 offset = 0; offset < length; offset += tuple_size) {
                            const u8 *entry = p + offset;
                            range physical = {
                                cells_value(entry, address_cells),
                                cells_value(entry + address_cells * 4, size_cells)
                            };
                            range translated;
                            if (!physical.length || !translate_fdt_range(nodes, depth, physical, &translated))
                                continue;
                            /* Device metadata is optional; keep booting if the bounded list fills. */
                            if (*range_count < BYTER_PLATFORM_MAX_RANGES)
                                ranges[(*range_count)++] = translated;
                        }
                    }
                }
            }
            p = (const u8 *)(((u64)(p + length) + 3) & ~3ULL);
            if (p > st_end) return 0;
        } else if (token == 4) {
        } else if (token == 9) {
            ended = 1;
            break;
        } else {
            return 0;
        }
    }
    return ended && depth == 0;
}

int byter_platform_fdt_read_boot_info(u8 *dtb, struct byter_platform_boot_info *info) {
    if (!info) return 0;
    u32 root_address_cells = 0, root_size_cells = 0;
    info->dtb_size = 0;
    info->memory_count = 0;
    info->reserved_count = 0;
    if (!parse_fdt(dtb, &info->dtb_size, info->memory, &info->memory_count,
                   info->reserved, &info->reserved_count, &info->framebuffer,
                   &root_address_cells, &root_size_cells)) return 0;
    return parse_fdt_device_ranges(dtb, info->dtb_size, root_address_cells,
                                   root_size_cells, info->reserved,
                                   &info->reserved_count);
}
