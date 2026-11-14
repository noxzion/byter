#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess

PAGE_SIZE = 4096
MAX_IMAGE_SIZE = 64 * 1024 * 1024
HANDOFF_OFFSET = 1024 * 1024
CATALOG_OFFSET_FIELD = 72
CATALOG_SIZE_FIELD = 80
FILE_SIZE_FIELD = 88
MAX_CONFIG_SIZE = 4096
MAX_CMDLINE_SIZE = 1024
MAX_LABEL_SIZE = 63
MAX_OPTIONS = 16
CATALOG_MAGIC = b"BYTERCAT"
CATALOG_VERSION = 1
CATALOG_HEADER = struct.Struct("<8sIIII")
CATALOG_ENTRY = struct.Struct("<IIQQII")


def align(value, alignment=PAGE_SIZE):
    return (value + alignment - 1) & ~(alignment - 1)


def load_config(path):
    if path.stat().st_size > MAX_CONFIG_SIZE:
        raise SystemExit(f"config exceeds {MAX_CONFIG_SIZE} bytes: {path}")
    legacy = {}
    entries = []
    current = None
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise SystemExit(f"invalid config line {line_number}: expected key=value")
        key, value = line.split("=", 1)
        key, value = key.strip().lower(), value.strip()
        if key == "entry":
            if current is not None:
                entries.append(current)
            if legacy:
                raise SystemExit("do not mix legacy kernel/cmdline with entry= blocks")
            current = {"name": value}
            continue
        if key not in {"kernel", "cmdline"}:
            raise SystemExit(f"unknown config key on line {line_number}: {key}")
        values = current if current is not None else legacy
        if key in values:
            raise SystemExit(f"duplicate {key}= on line {line_number}")
        values[key] = value

    if current is not None:
        entries.append(current)
    if entries and legacy:
        raise SystemExit("do not mix legacy kernel/cmdline with entry= blocks")
    if not entries:
        legacy["name"] = "Byter"
        entries = [legacy]
    if not entries[0].get("kernel"):
        raise SystemExit("each entry= block must set kernel=...")
    if len(entries) > MAX_OPTIONS:
        raise SystemExit(f"config supports at most {MAX_OPTIONS} boot entries")

    for entry in entries:
        try:
            label = entry.get("name", "").encode("ascii", errors="strict")
        except UnicodeEncodeError:
            raise SystemExit("entry names must use printable ASCII characters") from None
        if (not label or len(label) > MAX_LABEL_SIZE or
                any(byte < 0x20 or byte > 0x7e for byte in label)):
            raise SystemExit(f"entry name must be 1..{MAX_LABEL_SIZE} printable ASCII characters")
        if not entry.get("kernel"):
            raise SystemExit(f"entry {entry['name']!r} must set kernel=...")
        cmdline = entry.get("cmdline", "").encode("utf-8")
        if len(cmdline) > MAX_CMDLINE_SIZE or b"\0" in cmdline:
            raise SystemExit(f"cmdline must be at most {MAX_CMDLINE_SIZE} bytes and contain no NUL")
        kernel = pathlib.Path(entry["kernel"])
        if not kernel.is_absolute():
            kernel = path.parent / kernel
        entry["kernel_path"] = kernel.resolve()
        entry["label_bytes"] = label
        entry["cmdline_bytes"] = cmdline
    if len({entry["label_bytes"] for entry in entries}) != len(entries):
        raise SystemExit("boot entry names must be unique")
    return entries


def main():
    parser = argparse.ArgumentParser(description="Pack Byter ARM64 shim + ELF as an Android boot image")
    parser.add_argument("--shim", required=True, type=pathlib.Path)
    parser.add_argument("--config", required=True, type=pathlib.Path)
    parser.add_argument("--elf", type=pathlib.Path, help="override kernel= from the config")
    parser.add_argument("--kernel-out", required=True, type=pathlib.Path)
    parser.add_argument("--boot-out", required=True, type=pathlib.Path)
    parser.add_argument("--mkbootimg", default="mkbootimg")
    args = parser.parse_args()

    entries = load_config(args.config)
    if args.elf:
        if len(entries) != 1:
            raise SystemExit("--elf can override kernel= only with a single boot entry")
        entries[0]["kernel_path"] = args.elf.resolve()
    shim = bytearray(args.shim.read_bytes())
    if len(shim) < FILE_SIZE_FIELD + 8 or shim[56:60] != b"ARM\x64":
        raise SystemExit("shim is missing the ARM64 Image header or pack metadata")

    elf_blobs = []
    for entry in entries:
        elf = entry["kernel_path"].read_bytes()
        if not elf.startswith(b"\x7fELF") or len(elf) > MAX_IMAGE_SIZE:
            raise SystemExit(f"{entry['kernel_path']} must be an ELF file that fits in the 64 MiB image window")
        elf_blobs.append(elf)

    catalog_offset = align(len(shim))
    strings = bytearray()
    string_offsets = []
    for entry in entries:
        label_offset = CATALOG_HEADER.size + CATALOG_ENTRY.size * len(entries) + len(strings)
        strings += entry["label_bytes"] + b"\0"
        cmdline_offset = CATALOG_HEADER.size + CATALOG_ENTRY.size * len(entries) + len(strings)
        strings += entry["cmdline_bytes"] + b"\0"
        string_offsets.append((label_offset, len(entry["label_bytes"]),
                               cmdline_offset, len(entry["cmdline_bytes"]) + 1))
    catalog_size = CATALOG_HEADER.size + CATALOG_ENTRY.size * len(entries) + len(strings)
    if catalog_size > 0xffffffff:
        raise SystemExit("boot catalog is too large")

    catalog_end = catalog_offset + catalog_size
    elf_offsets = []
    cursor = align(catalog_end)
    for elf in elf_blobs:
        elf_offsets.append(cursor)
        cursor = align(cursor + len(elf))
    catalog = bytearray(CATALOG_HEADER.pack(CATALOG_MAGIC, CATALOG_VERSION, len(entries),
                                             CATALOG_ENTRY.size, catalog_size))
    for entry, offsets, elf_offset, elf in zip(entries, string_offsets, elf_offsets, elf_blobs):
        label_offset, label_length, cmdline_offset, cmdline_size = offsets
        catalog += CATALOG_ENTRY.pack(label_offset, label_length, elf_offset, len(elf),
                                      cmdline_offset, cmdline_size)
    catalog += strings

    kernel = shim + bytes(catalog_offset - len(shim)) + catalog
    for elf_offset, elf in zip(elf_offsets, elf_blobs):
        kernel += bytes(elf_offset - len(kernel)) + elf
    if len(kernel) > MAX_IMAGE_SIZE - HANDOFF_OFFSET:
        raise SystemExit("boot entries overlap the reserved handoff area in the 64 MiB image window")

    struct.pack_into("<Q", kernel, 16, MAX_IMAGE_SIZE)
    struct.pack_into("<Q", kernel, CATALOG_OFFSET_FIELD, catalog_offset)
    struct.pack_into("<Q", kernel, CATALOG_SIZE_FIELD, catalog_size)
    struct.pack_into("<Q", kernel, FILE_SIZE_FIELD, len(kernel))
    args.kernel_out.parent.mkdir(parents=True, exist_ok=True)
    args.kernel_out.write_bytes(kernel)

    args.boot_out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        args.mkbootimg,
        "--header_version", "4",
        "--pagesize", str(PAGE_SIZE),
        "--kernel", str(args.kernel_out),
        "--os_version", "0.0.0",
        "--os_patch_level", "2026-11",
        "--cmdline", "",
        "-o", str(args.boot_out),
    ], check=True)
    print(f"Wrote {args.boot_out} ({args.boot_out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
