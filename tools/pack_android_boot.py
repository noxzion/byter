#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess

PAGE_SIZE = 4096
MAX_IMAGE_SIZE = 64 * 1024 * 1024
HANDOFF_OFFSET = 1024 * 1024
ELF_OFFSET_FIELD = 72
ELF_SIZE_FIELD = 80
FILE_SIZE_FIELD = 88


def main():
    parser = argparse.ArgumentParser(description="Pack Byter ARM64 shim + ELF as an Android boot image")
    parser.add_argument("--shim", required=True, type=pathlib.Path)
    parser.add_argument("--elf", required=True, type=pathlib.Path)
    parser.add_argument("--kernel-out", required=True, type=pathlib.Path)
    parser.add_argument("--boot-out", required=True, type=pathlib.Path)
    parser.add_argument("--mkbootimg", default="mkbootimg")
    args = parser.parse_args()

    shim = bytearray(args.shim.read_bytes())
    elf = args.elf.read_bytes()
    if len(shim) < FILE_SIZE_FIELD + 8 or shim[56:60] != b"ARM\x64":
        raise SystemExit("shim is missing the ARM64 Image header or pack metadata")
    if not elf.startswith(b"\x7fELF") or len(elf) > MAX_IMAGE_SIZE:
        raise SystemExit("input must be an ELF file that fits in the 64 MiB shim window")

    elf_offset = (len(shim) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)
    kernel = shim + bytes(elf_offset - len(shim)) + elf
    if len(kernel) > MAX_IMAGE_SIZE - HANDOFF_OFFSET:
        raise SystemExit("shim and ELF overlap the reserved handoff area in the 64 MiB image window")

    struct.pack_into("<Q", kernel, 16, MAX_IMAGE_SIZE)
    struct.pack_into("<Q", kernel, ELF_OFFSET_FIELD, elf_offset)
    struct.pack_into("<Q", kernel, ELF_SIZE_FIELD, len(elf))
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
