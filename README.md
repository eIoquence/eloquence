# Eloquence

Eloquence is an experimental, hardware-first exokernel research project. It explores how an operating system can give software explicit, controlled ownership of hardware resources while retaining isolation and a measurable kernel boundary.

## Research focus

The initial research focus is a hardware resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

The first functional baseline targets x86-64 under QEMU. AArch64 and RV64 have Limine bootstrap sources and linked-ELF CI targets; MIPS32 has a compile-only interrupt primitive. No added architecture has a repeatable boot observation. The first hardware-facing slice is PCI device discovery and controlled MMIO access. DMA/IOMMU and physical-hardware performance claims remain later work.

The project does not claim novelty for capability systems or exokernels in general. Research claims will be made only after comparison with prior systems and reproducible experiments.

## Current status

Kernel bring-up is in progress. The x86-64 source includes Limine boot information, serial diagnostics, a bounded reclaiming page allocator, PCI inventory, a targeted QEMU EDU driver, and an experimental ring-3 MMIO demo using kernel-owned domain identity and scalar syscalls. The demo includes owner and foreign access, writable and read-only rights, register restoration, revocation, stale and malformed handles, invalid syscall rejection, and user-fault termination scenarios. A bounded block-device registry and optional legacy ATA PIO driver provide polled 28-bit LBA reads and writes; discovery issues IDENTIFY, followed by read-only primary GPT discovery with header/entry CRC checks. Neither path writes disk sectors. These paths still need a boot observation. Capability handles remain predictable slot/generation values, so the demo does not claim unforgeability. See:

- [Hardware Ownership Research](docs/research/hardware-ownership.md)
- [System Research Roadmap](docs/research/system-roadmap.md)
- [Experiment Record Schema](docs/research/experiment-record.schema.json)
- [Experimental x86-64 Syscall ABI](docs/abi/x86_64-syscall.md)
- [QEMU EDU PCI Driver](docs/drivers/qemu-edu.md)
- [Legacy ATA PIO Driver](docs/drivers/ata-pio.md)
- [Read-only GPT Discovery](docs/drivers/gpt.md)
- [Multi-Architecture and FPGA Research Plan](docs/research/multi-architecture-and-fpga.md)
- [ADR-0001: Research Scope and Initial Platform](docs/adr/0001-research-scope-and-platform.md)
- [ADR-0002: Bootstrap Physical Page Allocation](docs/adr/0002-bootstrap-page-allocation.md)
- [ADR-0003: Initial PCI Configuration Discovery](docs/adr/0003-pci-configuration-discovery.md)
- [ADR-0004: QEMU EDU Resource Sizing](docs/adr/0004-qemu-edu-resource-sizing.md)
- [ADR-0005: Bootstrap MMIO Mapping](docs/adr/0005-bootstrap-mmio-mapping.md)
- [ADR-0006: MMIO Capability Lifecycle](docs/adr/0006-mmio-capability-lifecycle.md)
- [ADR-0007: Early CPU Exception Diagnostics](docs/adr/0007-early-cpu-exception-diagnostics.md)
- [ADR-0008: Multi-Architecture and FPGA Research Program](docs/adr/0008-multi-architecture-and-fpga-research.md)
- [ADR-0009: User-Mode Capability Boundary](docs/adr/0009-user-mode-capability-boundary.md)

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

Prerequisites: GNU Make, Clang/LLVM with LLD, `curl`, `tar`, and `xorriso`. Running the x86-64 image also requires `qemu-system-x86_64`.

GitHub Actions builds the x86-64 ISO and linked AArch64 and RV64 ELF kernels on pushes and pull requests. It also compiles the current MIPS32 interrupt primitive; that target is not yet a kernel image. Build artifacts are retained for 14 days. The workflow compiles and packages the outputs without launching an emulator. Dependabot checks the GitHub Actions dependencies weekly.

```sh
make iso
make run
```

Compile the x86-64 kernel objects without linking or booting an image, or build
the architecture targets without starting an image:

```sh
make x86_64-objects
make aarch64-elf
make riscv64-elf
make mips32-objects
```

These targets do not boot an image. AArch64 and RV64 produce linked ELF files;
MIPS32 currently compiles only its interrupt-state primitive and is not a
kernel image. AArch64 still needs PL011 setup through a device-tree-derived
MMIO mapping and a repeatable boot observation. Until that UART path exists,
the bootstrap requires a compatible framebuffer and halts without visible
diagnostics if Limine provides none.

The RV64 bootstrap requests Sv39 and uses the SBI legacy console for early
diagnostics. The installed local Clang lacks a RISC-V target backend; GitHub
Actions builds and links the RV64 ELF. It has no repeatable boot observation.

The build downloads the pinned Limine v12.9.6 binary release and checks its SHA-256 before use. `make clean` removes generated files and the downloaded bootloader.

The Limine protocol header is vendored from [limine-protocol](https://github.com/Limine-Bootloader/limine-protocol) at commit `6e85ddd6f928b5b512015c4d47d1cfce2fd28079`; its 0BSD notice is preserved in `include/limine.h`.

The kernel requests bootloader information, the physical memory map, and the HHDM offset, then reports them over COM1. A single-CPU bootstrap allocator hands out and reclaims up to 8,192 tracked 4 KiB pages from usable regions. The QEMU EDU driver probes only its known BAR0, maps it uncached, checks the device identity, and verifies/restores its liveness register without enabling bus mastering. PCI discovery otherwise remains read-only. The x86-64 source enters ring 3 and routes scalar MMIO operations through `SYSCALL`; the kernel derives the actor from current-domain state. User exceptions are intended to terminate the demo domain, while kernel exceptions still halt; this path is compile-only until a repeatable boot observation is recorded. Capability handles are predictable slot/generation values and do not yet establish unforgeability. A bounded block-device registry is backed by an optional legacy ATA PIO driver; its discovery issues IDENTIFY only, while reads and writes require explicit callers. A read-only parser validates the primary GPT header and entry-array CRCs, but does not inspect the backup GPT. Writes may remain in volatile device cache until `block_device_flush` succeeds. The ATA path supports only 512-byte sectors and 28-bit LBA, polls with a finite timeout, and has no interrupt, AHCI, DMA, or filesystem support. User code and stack frames are reclaimed after the demo, while page-table frames are retained; external interrupts are not handled.
