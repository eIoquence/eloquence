# Eloquence

Eloquence is an experimental, hardware-first exokernel research project. It explores how an operating system can give software explicit, controlled ownership of hardware resources while retaining isolation and a measurable kernel boundary.

## Research focus

The initial research focus is a hardware resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

The first prototype will target x86-64 under QEMU. The first hardware-facing slice is PCI device discovery and controlled MMIO access. DMA/IOMMU, physical-hardware performance claims, and additional CPU architectures are later work.

The project does not claim novelty for capability systems or exokernels in general. Research claims will be made only after comparison with prior systems and reproducible experiments.

## Current status

Kernel bring-up is in progress. The source includes Limine boot information, serial diagnostics, a monotonic page allocator, read-only PCI inventory, a targeted BAR size probe, and a kernel-only MMIO capability exercise for QEMU's educational PCI device. Runtime validation is pending; user-domain isolation and user-facing capability handoff remain future work. See:

- [Hardware Ownership Research](docs/research/hardware-ownership.md)
- [ADR-0001: Research Scope and Initial Platform](docs/adr/0001-research-scope-and-platform.md)
- [ADR-0002: Bootstrap Physical Page Allocation](docs/adr/0002-bootstrap-page-allocation.md)
- [ADR-0003: Initial PCI Configuration Discovery](docs/adr/0003-pci-configuration-discovery.md)
- [ADR-0004: QEMU EDU Resource Sizing](docs/adr/0004-qemu-edu-resource-sizing.md)
- [ADR-0005: Bootstrap MMIO Mapping](docs/adr/0005-bootstrap-mmio-mapping.md)
- [ADR-0006: MMIO Capability Lifecycle](docs/adr/0006-mmio-capability-lifecycle.md)

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

The kernel requests bootloader information, the physical memory map, and the HHDM offset, then reports them over COM1. A single-CPU bootstrap allocator hands out 4 KiB pages from usable regions. PCI discovery reads conventional configuration space and reports function IDs and BAR fields. For the explicitly configured QEMU EDU device only, it temporarily disables I/O and memory decoding to size BAR0, restores the original registers, maps the range as uncached device memory, then transfers that mapping into a bounded MMIO capability table. The boot harness exercises authorized reads and writes, foreign-domain and read-only-write denials, revocation, stale-handle denial, and an audit ring. Domain IDs are currently supplied by trusted kernel code; there is no user mode or user-facing capability handoff yet. Page-table frames are not freed, and interrupts are not handled.
