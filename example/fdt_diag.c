#include "fdt_diag.h"

typedef struct {
    uint64_t base, length;
} checked_range;

typedef struct {
    uint32_t address_cells, size_cells;
    const uint8_t *ranges;
    uint32_t ranges_size;
    uint8_t has_ranges, excluded;
} fdt_context;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t read_cells(const uint8_t *p, uint32_t count) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < count; ++i) value = (value << 32) | read_be32(p + i * 4);
    return value;
}

static int text_equal(const char *text, const char *wanted) {
    while (*text && *text == *wanted) { ++text; ++wanted; }
    return *text == *wanted;
}

static int memory_node(const uint8_t *name) {
    static const char prefix[] = "memory";
    for (uint32_t i = 0; i < sizeof(prefix) - 1; ++i)
        if (name[i] != (uint8_t)prefix[i]) return 0;
    return name[sizeof(prefix) - 1] == 0 || name[sizeof(prefix) - 1] == '@';
}

static int address_fits(uint64_t base, uint64_t length, uint32_t cells) {
    if (!length || base > UINT64_MAX - (length - 1)) return 0;
    return cells == 2 || (cells == 1 && base <= UINT32_MAX &&
                           length - 1 <= UINT32_MAX - base);
}

static int translate_reg(fdt_context *nodes, uint32_t node_depth,
                         checked_range input, checked_range *output) {
    if (!address_fits(input.base, input.length, 2)) return 0;
    uint64_t address = input.base;
    for (uint32_t depth = node_depth - 1; depth > 1; --depth) {
        fdt_context *bus = &nodes[depth];
        uint32_t parent_cells = nodes[depth - 1].address_cells;
        if (!bus->has_ranges) return 0;
        if (!bus->ranges_size) {
            if (!address_fits(address, input.length, parent_cells)) return 0;
            continue;
        }

        uint32_t child_cells = bus->address_cells, size_cells = bus->size_cells;
        if (!child_cells || child_cells > 2 || !parent_cells || parent_cells > 2 ||
            !size_cells || size_cells > 2) return 0;
        uint32_t tuple_size = (child_cells + parent_cells + size_cells) * 4;
        if (bus->ranges_size % tuple_size) return 0;
        int found = 0;
        for (uint32_t offset = 0; offset < bus->ranges_size; offset += tuple_size) {
            const uint8_t *entry = bus->ranges + offset;
            uint64_t child_base = read_cells(entry, child_cells);
            uint64_t parent_base = read_cells(entry + child_cells * 4, parent_cells);
            uint64_t span = read_cells(entry + (child_cells + parent_cells) * 4, size_cells);
            if (!span || child_base > UINT64_MAX - span || address < child_base) continue;
            uint64_t delta = address - child_base;
            if (delta > span || input.length > span - delta || parent_base > UINT64_MAX - delta)
                continue;
            address = parent_base + delta;
            if (!address_fits(address, input.length, parent_cells)) continue;
            found = 1;
            break;
        }
        if (!found) return 0;
    }
    *output = (checked_range){address, input.length};
    return 1;
}

static int map_reserves(const struct byter_memmap_response *map, checked_range resource) {
    if (resource.base > UINT64_MAX - resource.length) return 0;
    uint64_t resource_end = resource.base + resource.length;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct byter_memmap_entry *entry = map->entries[i];
        if (!entry || entry->type != BYTER_MEMMAP_RESERVED || !entry->length ||
            entry->base > UINT64_MAX - entry->length) continue;
        if (resource.base >= entry->base && resource_end <= entry->base + entry->length)
            return 1;
    }
    return 0;
}

int byter_example_fdt_device_map_check(const void *blob,
                                      const struct byter_memmap_response *map) {
    if (!blob || !map || !map->entries || !map->entry_count || map->entry_count > 512)
        return 0;
    const uint8_t *dtb = blob;
    uint32_t total = read_be32(dtb + 4), struct_off = read_be32(dtb + 8);
    uint32_t strings_off = read_be32(dtb + 12), strings_size = read_be32(dtb + 32);
    uint32_t struct_size = read_be32(dtb + 36);
    if (total < 40 || total > 16 * 1024 * 1024 || struct_off > total ||
        struct_size > total - struct_off || strings_off > total ||
        strings_size > total - strings_off) return 0;

    const uint8_t *cursor = dtb + struct_off, *end = cursor + struct_size;
    const char *strings = (const char *)(dtb + strings_off);
    fdt_context nodes[32];
    for (uint32_t i = 0; i < 32; ++i) nodes[i] = (fdt_context){0};
    uint32_t depth = 0;
    while ((uint32_t)(end - cursor) >= 4) {
        uint32_t token = read_be32(cursor);
        cursor += 4;
        if (token == 1) {
            if (depth + 1 >= 32) return 0;
            const uint8_t *name = cursor;
            while (cursor < end && *cursor) ++cursor;
            if (cursor == end) return 0;
            cursor = (const uint8_t *)(((uintptr_t)(cursor + 1) + 3) & ~(uintptr_t)3);
            if (cursor > end) return 0;
            ++depth;
            nodes[depth].address_cells = depth == 1 ? 2 : nodes[depth - 1].address_cells;
            nodes[depth].size_cells = depth == 1 ? 2 : nodes[depth - 1].size_cells;
            nodes[depth].ranges = 0;
            nodes[depth].ranges_size = 0;
            nodes[depth].has_ranges = 0;
            nodes[depth].excluded = depth > 1 && nodes[depth - 1].excluded;
            if (depth == 2 && (memory_node(name) || text_equal((const char *)name, "chosen") ||
                text_equal((const char *)name, "reserved-memory") ||
                text_equal((const char *)name, "cpus"))) nodes[depth].excluded = 1;
        } else if (token == 2) {
            if (!depth) return 0;
            --depth;
        } else if (token == 3) {
            if ((uint32_t)(end - cursor) < 8) return 0;
            uint32_t length = read_be32(cursor), nameoff = read_be32(cursor + 4);
            cursor += 8;
            if (length > (uint32_t)(end - cursor) || nameoff >= strings_size) return 0;
            uint32_t n = nameoff;
            while (n < strings_size && strings[n]) ++n;
            if (n == strings_size) return 0;
            const char *name = strings + nameoff;
            if (depth && text_equal(name, "#address-cells"))
                nodes[depth].address_cells = length == 4 ? read_be32(cursor) : 0;
            else if (depth && text_equal(name, "#size-cells"))
                nodes[depth].size_cells = length == 4 ? read_be32(cursor) : 0;
            else if (depth && text_equal(name, "ranges")) {
                nodes[depth].has_ranges = 1;
                nodes[depth].ranges = cursor;
                nodes[depth].ranges_size = length;
            } else if (depth > 1 && !nodes[depth].excluded && text_equal(name, "reg")) {
                uint32_t ac = nodes[depth - 1].address_cells;
                uint32_t sc = nodes[depth - 1].size_cells;
                if (ac && ac <= 2 && sc && sc <= 2 && length % ((ac + sc) * 4) == 0) {
                    uint32_t tuple_size = (ac + sc) * 4;
                    for (uint32_t offset = 0; offset < length; offset += tuple_size) {
                        checked_range resource = {
                            read_cells(cursor + offset, ac),
                            read_cells(cursor + offset + ac * 4, sc)
                        }, physical;
                        if (resource.length && translate_reg(nodes, depth, resource, &physical) &&
                            map_reserves(map, physical)) return 1;
                    }
                }
            }
            cursor = (const uint8_t *)(((uintptr_t)(cursor + length) + 3) & ~(uintptr_t)3);
            if (cursor > end) return 0;
        } else if (token == 4) {
            continue;
        } else if (token == 9) {
            return 0;
        } else {
            return 0;
        }
    }
    return 0;
}
