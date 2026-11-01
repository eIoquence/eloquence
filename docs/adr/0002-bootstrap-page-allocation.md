# ADR-0002: Bootstrap Physical Page Allocation

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The kernel can inspect the Limine physical memory map but does not yet manage physical pages. MMIO experiments will need a clear conversion between physical addresses and the kernel's direct map, as well as a way to obtain page-sized physical frames.

## Decision

1. Request the Limine Higher Half Direct Map (HHDM) offset and the physical memory map.
2. Start with a single-CPU, monotonic 4 KiB page allocator over `LIMINE_MEMMAP_USABLE` regions.
3. Skip physical page zero and return both the physical address and the HHDM virtual address for each allocation.
4. Do not reclaim bootloader-reclaimable memory or provide page freeing in this bootstrap allocator.

## Consequences

- The allocator has constant state and predictable first-fit behavior without needing an allocator to store its own metadata.
- Allocated pages cannot yet be returned, and the allocator is not safe for concurrent callers. Both constraints are acceptable before interrupts, user domains, or SMP are enabled.
- HHDM mappings are an early-boot convenience; later virtual-memory ownership must track permissions and mapping lifetime explicitly.

## Revisit when

Revisit after early allocations need to be reclaimed, multiple CPUs can allocate concurrently, or page-level ownership becomes part of the capability model.
