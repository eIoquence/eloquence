# Eloquence

Eloquence is an experimental, hardware-first exokernel research project. It explores how an operating system can give software explicit, controlled ownership of hardware resources while retaining isolation and a measurable kernel boundary.

## Research focus

The initial research focus is a hardware resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

The first prototype will target x86-64 under QEMU. The first hardware-facing slice is PCI device discovery and controlled MMIO access. DMA/IOMMU, physical-hardware performance claims, and additional CPU architectures are later work.

The project does not claim novelty for capability systems or exokernels in general. Research claims will be made only after comparison with prior systems and reproducible experiments.

## Current status

Research foundation. The repository is defining its initial architecture and experiment before adding kernel subsystems. See:

- [Hardware Ownership Research](docs/research/hardware-ownership.md)
- [ADR-0001: Research Scope and Initial Platform](docs/adr/0001-research-scope-and-platform.md)

## v0.1 target

- Boot a minimal x86-64 kernel in QEMU through Limine.
- Emit boot and diagnostic output over a serial console.
- Inspect the bootloader-provided physical memory map.
- Define resource identities, rights, ownership transitions, and revocation semantics.
- Enumerate emulated PCI devices and prototype capability-checked MMIO access.
- Record functional outcomes and repeatable measurements with the exact emulator configuration.

QEMU is the functional baseline. Timing results from emulation will not be presented as physical-hardware performance results.

## Development

Build and run instructions will be added with the first bootable kernel milestone. Until then, the research and architecture documents are the source of truth for the v0.1 scope.
