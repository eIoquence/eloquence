# ADR-0001: Research Scope and Initial Platform

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The repository is an empty starting point. The project direction is hardware-first exokernel research, with an eventual interest in explicit ownership and controlled access to devices. Building for several CPU architectures, DMA, and physical hardware immediately would make it difficult to establish a reproducible first result.

## Decision

1. Treat hardware resource ownership and its lifecycle as the central research direction.
2. Use x86-64 under QEMU as the first functional platform.
3. Use Limine as the initial boot protocol so the project can focus on kernel behavior rather than a custom bootloader.
4. Start with a minimal kernel bring-up slice: boot information, serial diagnostics, and inspection of the physical memory map.
5. Make PCI discovery and bounded MMIO grant/revoke the first device-facing experiment, after the required memory and protection mechanisms exist.
6. Keep DMA/IOMMU, physical-hardware performance evaluation, and additional architecture ports outside v0.1. The later multi-architecture research program is defined in [ADR-0008](0008-multi-architecture-and-fpga-research.md).

## Consequences

- The initial result is a controlled functional prototype, not a complete operating system.
- QEMU results can validate protocol behavior and repeatable functional cases; they cannot establish physical-hardware performance.
- The capability representation, exact isolation mechanism, and revocation guarantees remain open until a dedicated design record is written.
- Additional ports will be evaluated after the first platform has stable interfaces and experiments; see ADR-0008 for the accepted sequence.

## Revisit when

Revisit this decision when the first bootable kernel and resource lifecycle prototype exist, or when an experiment demonstrates that the chosen platform cannot answer the research question.
