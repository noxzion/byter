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
- Response objects are revision 0. Pointers are direct address values with no
  HHDM offset. The shim does not establish page tables, so pointer access relies
  on the inherited ARM64 translation state; the example verifies the current
  A059 framebuffer and memory-map path.

## Implemented requests

| Request | Request revision | Response revision | Fields provided | Source and limits |
| --- | ---: | ---: | --- | --- |
| `BYTER_BOOTLOADER_INFO_REQUEST_ID` | 0 | 0 | `name`, `version` | Static strings `Byter AArch64 shim` and `0.1`. |
| `BYTER_DTB_REQUEST_ID` | 0 | 0 | `dtb_ptr` | Original DTB pointer received from ABL. The response does not provide a DTB size. |
| `BYTER_FRAMEBUFFER_REQUEST_ID` | 0 | 0 | One framebuffer pointer and its address, dimensions, pitch, bpp, RGB masks, and memory model | Parsed from a DTB `simple-framebuffer` node; otherwise the A059 fallback profile. If no framebuffer is available, `response` remains `NULL`. EDID and video modes are not provided. |
| `BYTER_MEMMAP_REQUEST_ID` | 0 | 0 | `entry_count`, pointer array of `{base, length, type}` entries | RAM ranges from DTB, split around known reservations, shim, DTB, ELF `PT_LOAD` ranges, and framebuffer. Limited to 512 output entries; non-RAM address space and unrecognized device ranges are omitted. |
| `BYTER_EXECUTABLE_ADDRESS_REQUEST_ID` | 0 | 0 | `physical_base`, `virtual_base` | Lowest loaded `PT_LOAD` base. They are equal because the loader currently requires `p_paddr == p_vaddr`; no relocation or separate VMA/PADDR mapping is supported yet. |

## Declared but not implemented

The following IDs are declared in `byter.h`, but the shim does not implement
their responses. Requests using them receive `response == NULL`:

| Request | Request | Request |
| --- | --- | --- |
| `BYTER_EXECUTABLE_CMDLINE_REQUEST_ID` | `BYTER_FIRMWARE_TYPE_REQUEST_ID` | `BYTER_STACK_SIZE_REQUEST_ID` |
| `BYTER_HHDM_REQUEST_ID` | `BYTER_FLANTERM_FB_INIT_PARAMS_REQUEST_ID` | `BYTER_PAGING_MODE_REQUEST_ID` |
| `BYTER_MP_REQUEST_ID` | `BYTER_ENTRY_POINT_REQUEST_ID` | `BYTER_EXECUTABLE_FILE_REQUEST_ID` |
| `BYTER_MODULE_REQUEST_ID` | `BYTER_RSDP_REQUEST_ID` | `BYTER_SMBIOS_REQUEST_ID` |
| `BYTER_EFI_SYSTEM_TABLE_REQUEST_ID` | `BYTER_TPM_EVENT_LOG_REQUEST_ID` | `BYTER_EFI_MEMMAP_REQUEST_ID` |
| `BYTER_DATE_AT_BOOT_REQUEST_ID` | `BYTER_RISCV_BSP_HARTID_REQUEST_ID` | `BYTER_BOOTLOADER_PERFORMANCE_REQUEST_ID` |
| `BYTER_X86_64_KEEP_IOMMU_REQUEST_ID` | `BYTER_TSC_FREQUENCY_REQUEST_ID` | `BYTER_ENTROPY_REQUEST_ID` |

This list is intentionally based on the IDs currently declared in the public
header. Adding a response requires implementing its data source and handoff
semantics in the shim, updating this table, and verifying it with a kernel
requesting that feature.
