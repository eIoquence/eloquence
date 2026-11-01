# ADR-0005: Bootstrap MMIO Mapping

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The QEMU EDU BAR is not guaranteed to be covered by Limine's Higher Half Direct Map (HHDM). Under the requested Limine base revision, the HHDM includes only specified memory-map types; ordinary reserved memory is not among them. A device BAR therefore needs an explicit mapping before the kernel can access it.

## Decision

1. Request four-level x86-64 paging for the initial mapper.
2. Reserve a 64 MiB kernel virtual aperture at the next 512 GiB boundary after Limine's HHDM mappings, below the kernel image. This gives the aperture a separate PML4 entry from the current direct map.
3. Map 4 KiB pages with write-through and cache-disable bits to select uncached memory through Limine's PAT layout, and set NX when enabled.
4. Reject a requested range when its page-rounded extent overlaps any memory-map type Limine maps in the HHDM. This prevents a second mapping of the same physical pages with a different cache type.
5. Keep mapping handles kernel-private. Unmap clears each leaf entry, invalidates the corresponding TLB entry, and releases the aperture pages for reuse.
6. Keep the first implementation single-CPU. Page-table pages come from the monotonic allocator and are not reclaimed on unmap.

## Consequences

- The EDU MMIO exercise can use an explicit uncached mapping instead of assuming a direct-map alias exists.
- This implements kernel mapping and revocation only. It does not create a user protection domain or demonstrate capability authorization.
- The dynamically selected aperture and four-level paging request are initial-platform choices that need revisiting before SMP, user domains, or support for a different paging layout.
- A failed map may leave newly allocated intermediate page tables in place; the allocator cannot free those pages yet.

## Revisit when

The project adds user address spaces, SMP, a general virtual-memory allocator, a paging-mode change, or MMIO resources that overlap HHDM-mapped ranges.

## References

- [Limine protocol: base revision mapping and x86-64 caching](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md#base-revision-4)
- [QEMU EDU device specification](https://www.qemu.org/docs/master/specs/edu.html)
