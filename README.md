# Eloquence

Eloquence is an experimental, hardware-first exokernel research project. It explores how an operating system can give software explicit, controlled ownership of hardware resources while retaining isolation and a measurable kernel boundary.

## Research focus

The initial research focus is a hardware resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

The first prototype will target x86-64 under QEMU. The first hardware-facing slice is PCI device discovery and controlled MMIO access. DMA/IOMMU, physical-hardware performance claims, and additional CPU architectures are later work.

The project does not claim novelty for capability systems or exokernels in general. Research claims will be made only after comparison with prior systems and reproducible experiments.

## Current status

Kernel bring-up is in progress. The source now includes Limine boot information, serial diagnostics, a monotonic page allocator, and read-only PCI discovery; local QEMU execution is pending. Controlled MMIO remains future work. See:

- [Hardware Ownership Research](docs/research/hardware-ownership.md)
- [ADR-0001: Research Scope and Initial Platform](docs/adr/0001-research-scope-and-platform.md)
- [ADR-0002: Bootstrap Physical Page Allocation](docs/adr/0002-bootstrap-page-allocation.md)

## v0.1 target

- Boot a minimal x86-64 kernel in QEMU through Limine.
- Emit boot and diagnostic output over a serial console.
- Inspect the bootloader-provided physical memory map.
- Allocate a bootstrap 4 KiB physical page from usable memory.
- Define resource identities, rights, ownership transitions, and revocation semantics.
- Enumerate emulated PCI devices and prototype capability-checked MMIO access.
- Record functional outcomes and repeatable measurements with the exact emulator configuration.

QEMU is the functional baseline. Timing results from emulation will not be presented as physical-hardware performance results.

## Development

Prerequisites: GNU Make, Clang/LLVM with LLD, `curl`, `tar`, and `xorriso`. Running the image also requires `qemu-system-x86_64`.

```sh
make iso
make run
```

The build downloads the pinned Limine v12.9.6 binary release and checks its SHA-256 before use. `make clean` removes generated files and the downloaded bootloader.

The Limine protocol header is vendored from [limine-protocol](https://github.com/Limine-Bootloader/limine-protocol) at commit `6e85ddd6f928b5b512015c4d47d1cfce2fd28079`; its 0BSD notice is preserved in `include/limine.h`.

The kernel requests bootloader information, the physical memory map, and the HHDM offset, then reports them over COM1. A single-CPU bootstrap allocator can hand out 4 KiB pages from usable regions. PCI discovery reads conventional configuration space and reports function IDs and BAR base fields; it does not size BARs, map MMIO, free pages, handle interrupts, or grant hardware capabilities yet.
