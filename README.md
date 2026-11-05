# Eloquence

Eloquence is an experimental, hardware-first exokernel research project. It explores how an operating system can give software explicit, controlled ownership of hardware resources while retaining isolation and a measurable kernel boundary.

## Research focus

The initial research focus is a hardware resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

The first functional baseline targets x86-64 under QEMU. AArch64 and RV64 have Limine bootstrap sources and linked-ELF CI targets; MIPS32 has a compile-only interrupt primitive. The x86-64 monitor, PIT/APIC clock, EDU driver, ring-3 capability scenarios, VirtIO block driver, GPT discovery, root FAT32 reads and bounded mutations, and bounded static ELF launch now boot and run under QEMU. The FAT32 fixture includes a compiled ring-3 shell that auto-starts with non-blocking serial input, scalar character output, and buffered single-process pipelines for `echo`, `uname`, `version`, `grep`, and `wc`. No other architecture has a repeatable boot observation. Scheduling, subdirectory traversal, script files, kernel IPC pipes, journaled storage, IOMMU isolation, and physical-hardware performance claims remain later work.

The project does not claim novelty for capability systems or exokernels in general. Research claims will be made only after comparison with prior systems and reproducible experiments.

## Current status

Kernel bring-up is in progress. The x86-64 source includes Limine boot information, serial diagnostics, a bounded reclaiming page allocator, PCI inventory, a targeted QEMU EDU driver, an experimental ring-3 MMIO demo using kernel-owned domain identity and scalar syscalls, a polling VirtIO block driver, a primary GPT parser, a single-root FAT32 VFS with bounded file create/write/delete, and a bounded static ELF loader. The demo passes owner read/write, foreign-domain denial, read-only denial, revocation, stale and malformed handle rejection, invalid syscall handling, and user-fault termination. A 100 Hz PIT clock reaches IRQ0 through the local APIC virtual-wire path. The COM1 monitor accepts bounded diagnostic commands, including `disk list`, `gpt list`, `fs list`, `fs cat`, and `run`; the ring-3 shell supports root file operations plus `ps`, synchronous `exec`, asynchronous `spawn`, and `kill`. The x86-64 kernel now round-robins runnable user processes on timer interrupts in QEMU. Shell pipelines buffer output sequentially in Ring 3 and currently support `echo`, `uname`, `version`, `grep`, and `wc`; they do not connect separate scheduled processes. Subdirectory traversal, `wait`, process IPC, script files, crash recovery, and a general application ecosystem remain future work. Capability handles remain predictable slot/generation values, so the demo does not claim unforgeability. See:

- [Hardware Ownership Research](docs/research/hardware-ownership.md)
- [System Research Roadmap](docs/research/system-roadmap.md)
- [Experiment Record Schema](docs/research/experiment-record.schema.json)
- [Experimental x86-64 syscall ABI](docs/abi/x86_64-syscall.md)
- [QEMU EDU PCI Driver](docs/drivers/qemu-edu.md)
- [Legacy ATA PIO Driver](docs/drivers/ata-pio.md)
- [VirtIO Block Driver](docs/drivers/virtio-block.md)
- [Read-only GPT Discovery](docs/drivers/gpt.md)
- [Read-only FAT32 Access](docs/drivers/fat32.md)
- [Static x86-64 ELF Loader](docs/userland/static-elf-loader.md)
- [x86-64 Serial Monitor](docs/drivers/serial-monitor.md)
- [Functional OS Path](docs/plans/2026-11-03-functional-os-path.md)
- [QEMU Storage and FAT32 Run](docs/experiments/2026-11-03-qemu-storage-fs.md)
- [QEMU Static ELF Process Run](docs/experiments/2026-11-03-qemu-elf-process.md)
- [QEMU User-Space Shell Run](docs/experiments/2026-11-03-qemu-userspace-shell.md)
- [QEMU Shell Pipeline Run](docs/experiments/2026-11-05-qemu-shell-pipelines.md)
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

Prerequisites: GNU Make, Clang/LLVM with LLD, `curl`, `tar`, `xorriso`, and Python 3. Running the x86-64 image also requires `qemu-system-x86_64`.

GitHub Actions builds the x86-64 ISO and user shell ELF, linked AArch64 and RV64 ELF kernels, and the current MIPS32 interrupt primitive on pushes and pull requests; MIPS32 is not yet a kernel image. Build artifacts are retained for 14 days. The workflow compiles and packages the outputs without launching an emulator. Dependabot checks the GitHub Actions dependencies weekly.

```sh
make iso
make userland-elf
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

The kernel requests bootloader information, the physical memory map, and the HHDM offset, then reports them over COM1. A single-CPU bootstrap allocator hands out and reclaims up to 8,192 tracked 4 KiB pages from usable regions. The QEMU EDU driver probes only its known BAR0, maps it uncached, checks the device identity, and verifies/restores its liveness register without enabling bus mastering. The VirtIO driver enables I/O and bus mastering for a matched transitional block device, uses a bounded polling queue and contiguous DMA buffers, and registers read/write/flush operations with the block layer. The ATA driver remains probe-only unless an explicit caller requests I/O. A read-only parser validates the primary GPT header and entry-array CRCs but does not inspect the backup GPT. FAT32 mounting validates volume bounds and metadata; the monitor can list and read short 8.3 files up to 4 KiB, while the ring-3 shell mutates root files with a 16 KiB write cap. The static ELF runner supports synchronous `exec` and timer-preempted `spawn` processes with independent address spaces and kernel stacks; `ps` reports bounded lifecycle history and `kill` can terminate another scheduled process. The syscall ABI remains experimental; subdirectory traversal, `wait`, IPC, journaling, and a general userland are absent. The polling serial monitor supports `help`, `hwinfo`, `mem`, `uptime`, `irq status`, `disk list`, `gpt list`, `fs list`, `fs cat`, `run`, and `halt`. See the [QEMU boot record](docs/experiments/2026-11-03-qemu-x86_64-first-boot.md), [storage run record](docs/experiments/2026-11-03-qemu-storage-fs.md), and [process lifecycle run](docs/experiments/2026-11-05-qemu-process-lifecycle.md).
