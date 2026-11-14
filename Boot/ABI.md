# Byter request ABI status

This document describes the behavior implemented by the current AArch64 shim.
The structures and request IDs in [`include/byter.h`](include/byter.h) retain
the Limine wire values, but this is a partial Byter implementation and does not
claim full Limine Boot Protocol compatibility.

## Common rules

- The current handoff uses the implicit base revision 0. The example does not
  include a base revision tag, and the shim does not parse or update such tags.
- Request objects are found at 8-byte-aligned addresses inside loaded ELF
  `PT_LOAD` segments. Before reading or writing a request, the shim checks that
  its 48-byte common header fits in that segment; it does not access
  request-specific trailing fields. The shim searches between the last start
  marker and the first end marker when the bounds are valid. With base revision
  0, absent or malformed delimiters fall back to searching the loaded image.
- Only request revision 0 is supported. For requests with the common Byter
  magic, the shim clears `response` first; unknown IDs and unsupported request
  revisions therefore remain `NULL`.
- Response objects are revision 0. All non-NULL pointers returned in responses
  and request `response` fields use the HHDM offset. The shim establishes 4 KiB
  page tables before entering the kernel: DTB-declared RAM and framebuffer are
  mapped at the HHDM offset; RAM, shim, DTB, handoff data, and framebuffer also
  remain identity-mapped. ELF segments receive higher-half mappings with
  permissions derived from `PT_LOAD` flags.

## Implemented requests

| Request | Request revision | Response revision | Fields provided | Source and limits |
| --- | ---: | ---: | --- | --- |
| `BYTER_BOOTLOADER_INFO_REQUEST_ID` | 0 | 0 | `name`, `version` | Static strings `Byter AArch64 shim` and `0.1`. |
| `BYTER_DTB_REQUEST_ID` | 0 | 0 | `dtb_ptr` | Original DTB pointer received from ABL. The response does not provide a DTB size. |
| `BYTER_FRAMEBUFFER_REQUEST_ID` | 0 | 0 | One framebuffer pointer and its address, dimensions, pitch, bpp, RGB masks, and memory model | Parsed from a DTB `simple-framebuffer` node; otherwise the A059 fallback profile. If no framebuffer is available, `response` remains `NULL`. EDID and video modes are not provided. |
| `BYTER_HHDM_REQUEST_ID` | 0 | 0 | `offset` | Fixed AArch64 HHDM base `0xffff800000000000`; maps DTB RAM ranges and framebuffer. |
| `BYTER_MEMMAP_REQUEST_ID` | 0 | 0 | `entry_count`, pointer array of `{base, length, type}` entries | RAM ranges from DTB, split around known reservations, shim, DTB, ELF `PT_LOAD` ranges, and framebuffer. Limited to 512 output entries; non-RAM address space and unrecognized device ranges are omitted. |
| `BYTER_EXECUTABLE_ADDRESS_REQUEST_ID` | 0 | 0 | `physical_base`, `virtual_base` | Lowest loaded `PT_LOAD` physical and virtual bases. The loader accepts AArch64 `ET_EXEC` segments with a uniform load bias, page-compatible addresses, and non-overlapping mappings; the example uses a higher-half VMA. |

The current handoff enters at EL1 with the MMU and caches enabled, using a
4 KiB translation granule and 48-bit virtual addressing. The HHDM covers the
DTB RAM ranges and framebuffer; it does not map device address ranges absent
from the DTB memory nodes. Identity mappings remain available for the same
areas. The shim expects the Android arm64 entry conditions (MMU and data cache
off) and stops if they are not met. This does not yet claim support for a
Limine base revision: in particular, base-revision-specific identity-map and
memory-map requirements are not all implemented.

## Declared but not implemented

The following IDs are declared in `byter.h`, but the shim does not implement
their responses. Requests using them receive `response == NULL`:

| Request | Request | Request |
| --- | --- | --- |
| `BYTER_EXECUTABLE_CMDLINE_REQUEST_ID` | `BYTER_FIRMWARE_TYPE_REQUEST_ID` | `BYTER_STACK_SIZE_REQUEST_ID` |
| `BYTER_FLANTERM_FB_INIT_PARAMS_REQUEST_ID` | `BYTER_PAGING_MODE_REQUEST_ID` | `BYTER_MP_REQUEST_ID` |
| `BYTER_ENTRY_POINT_REQUEST_ID` | `BYTER_EXECUTABLE_FILE_REQUEST_ID` | `BYTER_RSDP_REQUEST_ID` |
| `BYTER_MODULE_REQUEST_ID` | `BYTER_SMBIOS_REQUEST_ID` | `BYTER_EFI_SYSTEM_TABLE_REQUEST_ID` |
| `BYTER_TPM_EVENT_LOG_REQUEST_ID` | `BYTER_EFI_MEMMAP_REQUEST_ID` | `BYTER_DATE_AT_BOOT_REQUEST_ID` |
| `BYTER_RISCV_BSP_HARTID_REQUEST_ID` | `BYTER_BOOTLOADER_PERFORMANCE_REQUEST_ID` | `BYTER_X86_64_KEEP_IOMMU_REQUEST_ID` |
| `BYTER_TSC_FREQUENCY_REQUEST_ID` | `BYTER_ENTROPY_REQUEST_ID` | — |

This list is intentionally based on the IDs currently declared in the public
header. Adding a response requires implementing its data source and handoff
semantics in the shim, updating this table, and verifying it with a kernel
requesting that feature.
