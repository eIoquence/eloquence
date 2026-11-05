# Eloquence System Research Roadmap

**Status:** Working specification
**Version:** 0.2
**Research direction:** Hardware-first exokernel
**Scope:** Long-term program; this document distinguishes current code from planned work.

## Research objective

Eloquence studies how applications can receive explicit, bounded control over
hardware resources while the kernel preserves isolation and makes each
authorization and ownership transition observable.

The central question is:

> How can an exokernel expose hardware resources through explicit ownership and
> revocable capabilities while preserving isolation and keeping mediation
> costs measurable?

The primary candidate contribution is a Hardware Capability Ownership Model
(HCOM) and a resource lifecycle:

`Discovery → Authorization → Allocation → Ownership → Revocation → Release`

These are research hypotheses, not novelty claims. A literature review and
comparison with exokernel, microkernel, and resource-management systems must
precede claims that HCOM contributes a new mechanism.

## System architecture

```text
Hardware experiments · diagnostics · benchmarks · native applications
                         │
User-space libraries · policies · isolated device services
                         │ syscalls / IPC / shared memory
eloquence kernel: protection · capabilities · resource lifecycle · audit
                         │
Architecture mechanisms: CPU · page tables · TLB · interrupts · MMIO
                         │
Platform services: boot handoff · memory map · device tree · timers · buses
                         │
Physical hardware · QEMU · FPGA SoC
```

CPU architecture and board/platform support are separate boundaries. Common
resource policy must not contain architecture instructions, page-table formats,
or board-specific addresses. A backend is considered complete only after it
documents its guarantees for privilege, translation, cache attributes, memory
ordering, interrupts, discovery, and revocation.

CPU and GPU are independent hardware modules connected through a SoC
interconnect. A custom CPU, GPU, memory controller, cache hierarchy, DMA engine,
PCIe endpoint, or cryptographic accelerator is a separate hardware research
track. It follows the software baseline and has its own RTL, verification,
board, timing, and bitstream provenance; those devices are not kernel features
that can be claimed from an interface stub.

## Implementation status

| Area | Current state | Evidence required for the next claim |
| --- | --- | --- |
| x86-64 bootstrap | Limine boot, serial diagnostics, page allocator, exceptions, PCI discovery, QEMU EDU driver, uncached MMIO mapping, polling COM1 monitor, 100 Hz PIT/PIC clock via local APIC virtual wire | [QEMU boot record](../experiments/2026-11-03-qemu-x86_64-first-boot.md); repeat on a clean checkout and add automated run coverage |
| User protection | Ring-3 MMIO demo with scalar syscalls, kernel-owned domain identity, explicit page permissions, and user-fault termination | QEMU observed owner read/write, read-only denial, foreign-domain denial, stale-handle denial, and fault return; formal process lifecycle and adversarial coverage remain |
| AArch64 | Limine bootstrap sources and separate ELF target | Compile/link, explicit QEMU `virt` CPU/firmware, repeatable boot; then DTB-driven PL011 and GIC |
| RV64 | Limine bootstrap sources request Sv39 and report handoff via SBI | Compile/link with a RISC-V-capable toolchain, QEMU `virt` boot, DTB-derived devices and interrupt controller |
| MIPS32 | Interrupt-state primitive and compile-only target | Separate Malta reset/firmware path, linker/entry code, UART diagnostics, exception and TLB contract |
| FPGA | Research plan only; CVA6 is a candidate | Named board, RTL revision/configuration, firmware, RAM/peripherals, bitstream hash and reproducible boot |
| DMA / IOMMU | Not implemented | Buffer ownership protocol and isolation tests on a named compatible platform |
| Scheduler / IPC | Not implemented | Timer source, context-switch ABI, domain lifecycle, bounded message and capability transfer |
| Storage / network | Block-device API, legacy ATA PIO, polling VirtIO legacy PCI block driver, primary GPT parser, and a single-root read-only VFS backed by FAT32; shell `ls`, `cat`, `stat`, and `pwd` use it; [QEMU storage run](../experiments/2026-11-03-qemu-storage-fs.md) | Multiple mounts, path traversal/subdirectories, working directories, FAT32 writes, backup GPT recovery, modern VirtIO transport, network protocols, and reproducible workload records |
| SDK / userland | Kernel monitor plus bounded static ELF loader; `SHELL.ELF` auto-starts from FAT32 with line editing and `help`, `clear`, `echo`, `uname`, `uptime`, `version`, `sysinfo`, `meminfo`, `cpuinfo`, `devinfo`, `irqinfo`, `bootinfo`, `about`, and `exit` | Scheduler, process/service management, stable syscall/API contract, libraries, filesystem commands, and broader development tools |

Build success, boot success, functional support, and measured performance are
separate maturity levels. A CI artifact is a build result, not proof that a
platform boots or that its isolation works.

## Hardware and CPU research

The software program begins with existing processors and virtual platforms.
For CPU/SoC work, the eventual experimental design is:

- a documented and versioned ISA profile;
- an independently implemented SystemVerilog CPU with pipeline, registers,
  ALU, exception model, and MMU;
- physical-memory controller, cache hierarchy, timer and interrupt logic;
- CPU/GPU-independent modules joined by a documented SoC interconnect;
- UART, SPI, I2C, GPIO, and DMA controllers, with PCIe only on a board that
  exposes the required fabric and pins;
- hardware protection, access-control, and observable ownership transitions.

Every module needs an interface specification and a verification plan before
RTL implementation. FPGA results record the synthesis tool, board revision,
device, clock constraints, memory/peripheral map, RTL commit, generated
bitstream hash, and test procedure. The initial OS does not depend on a custom
processor or GPU.

## Firmware and boot chain

The current bootloader remains the first implementation boundary. A future
firmware program is staged as follows:

1. Record reset/firmware inputs and the exact CPU privilege level.
2. Initialize and describe physical memory.
3. Discover hardware through the platform source of truth (device tree, ACPI,
   or board contract) and expose only mapped resources to the kernel.
4. Verify signed firmware/kernel components against user-controlled keys.
5. Record measured boot events where TPM or platform measurement support exists.
6. Pass a versioned handoff structure to the kernel and preserve a recovery
   path that does not silently weaken verification.

Secure Boot, measured boot, key enrollment/rotation, recovery, and a custom
bootloader are not implemented. Their design must define key ownership,
revocation, rollback behavior, failure reporting, and recovery before code is
added. A successful signature check alone does not establish a secure boot
chain.

## Kernel subsystem plan

| Subsystem | First useful implementation | Later research direction |
| --- | --- | --- |
| CPU and exceptions | Per-architecture entry, fatal diagnostics, privilege transition | Context switching, PMU, exception latency, supported CPU features |
| Physical memory | Validated memory-map source and page allocation | Free/reclaim, ownership, fragmentation data, NUMA and memory pressure |
| Virtual memory | Isolated address spaces and page permissions | ASLR, lazy mappings, TLB shootdown, architecture-specific invalidation |
| Protection domains | Kernel-owned identity and user-mode boundary | Create/destroy, scheduling, lifecycle and fault containment |
| Capabilities | Bounded MMIO read rights, audit, revoke | Delegation, transfer, subranges, handle hardening and typed resources |
| Scheduler | Single-CPU timer-driven preemption | Policy experiments, priorities and application-controlled scheduling |
| Interrupts and timers | Platform controller initialization and acknowledged IRQs | IRQ capabilities, affinity, latency distributions and timer policy |
| IPC | Bounded message passing with explicit endpoint authority | Shared memory, capability transfer, backpressure and measurements |
| Device manager | Bounded block-device registry and PCI inventory; no general driver binding or hotplug | Stable device identity, user-space drivers, dependency and recovery policy |
| DMA / IOMMU | Explicit buffer state and map/unmap contract | Device domains, revocation guarantees, fault handling, platform evaluation |
| Syscalls | No user pointers in the initial MMIO syscall ABI | Validated buffer APIs, ABI versioning and compatibility policy |
| Resource manager | Discovery-to-release audit trail | Reservation, delegation, preemption and resource policy in user space |

No driver may treat a device as isolated merely because its MMIO BAR is
capability-checked. DMA can reach memory independently of CPU page tables; the
DMA ownership claim depends on a functioning IOMMU or another documented
hardware boundary.

## Security model

The trusted computing base starts with the boot handoff, architecture entry,
kernel memory manager, capability checks, and resource mapping code. User-space
drivers and applications are treated as potentially faulty. The project aims
to prevent one compromised driver from directly changing another domain's
kernel memory or accessing an ungranted resource.

The implementation sequence is:

1. Establish ring-3 execution, a kernel-owned current-domain identity, and
   syscalls that accept bounded scalar arguments rather than user pointers.
2. Prove authorization, denial, revocation, and audit ordering through actual
   untrusted code, not only through kernel-supplied domain IDs.
3. Add user address-space lifecycle, fault termination, and scheduling before
   adding IPC or user-space drivers.
4. Add ASLR and signed binary policy only after address-space and boot contracts
   exist.
5. Add DMA isolation only with a tested IOMMU/device boundary.
6. Add fuzzing and formal verification to narrow, stable interfaces such as
   capability transition validation and syscall decoding.

Current MMIO handles are bounded slot/generation values. They are not a
cryptographic or unforgeability guarantee: a future capability design must
prevent unauthorized handle derivation, specify delegation, and account for
entropy and revocation. The current domain identity is single-CPU kernel state;
it is not an SMP-safe scheduler contract.

Every sensitive operation should eventually record actor, resource identity,
operation, outcome, duration, and sequence. The current fixed-size audit ring
records a subset of these fields, reports how many older events it has
overwritten, and exposes only the latest events through a kernel diagnostic.
Future audit storage must remain bounded and define which data can be read by
user space.

## Storage, networking, and userland

These are separate services built after user domains and IPC are stable.

### Storage

The sequence is a block-device contract, VirtIO Block in QEMU, GPT parsing,
read-only FAT32/ext2 interoperability, then a small experimental EFS. The
block cache and crash-recovery/journaling design must be evaluated before
write support is treated as safe. NVMe and physical storage follow the virtual
baseline. Filesystem metadata and device commands remain untrusted inputs.

### Networking

Start with VirtIO Network in QEMU. Add Ethernet framing, IPv4 and IPv6,
ARP/Neighbor Discovery, UDP/TCP, DHCP and DNS, then socket APIs and firewall
policy. TLS should use a mature library once the ABI and cryptographic
environment are suitable. Physical MAC/PHY drivers follow platform-specific
bring-up. Packet parsing requires bounded lengths and fuzzable parsers.

### Userland and development ecosystem

The kernel-resident monitor currently provides `help`, `hwinfo`, `mem`,
`uptime`, `irq status`, `disk list`, `gpt list [device]`, and `halt` as
diagnostics. It polls COM1 and uses the PIT only as a clock; it is not a
scheduler. The first
native tools should grow into `hwinfo`, `top`, `net status`, `disk list`,
`services list`, and `log --follow`, backed by documented user-facing kernel
interfaces. Candidate components include `libeloq`, `eloqsh`, `eloqdbg`,
`eloqmon`, `eloqfs`, `eloqnet`, `eloqtest`, and `eloqbuild`; names are proposals
until their APIs and packages exist.

The SDK milestone includes a compiler/assembler/linker profile, syscall and
device API headers, a standard library subset, reproducible build files,
debugging symbols, emulator configurations, and small native examples. Kernel
APIs should not be advertised as stable until versioned and tested by an
independent user program.

## Experiments and comparison

QEMU is the repeatable functional baseline. Each run records source revision,
compiler/tool versions and flags, machine/CPU configuration, firmware,
device layout, memory size, and expected/observed result. No timing result
from QEMU is used as a physical-hardware performance claim.

The machine-readable [experiment record schema](experiment-record.schema.json)
defines the minimum metadata, raw measurements, outcomes, and limitations for
one run. A host-side collector and report generator remain future work.

For benchmarks, report raw samples and the operation definition, repetitions,
median, P50/P95/P99, spread, warm-up policy, and platform limitations. Initial
comparisons are direct access versus syscall mediation versus a user-space
library path, and allocation/map/revoke costs for the exact operation under
study. Thresholds are set after a stable baseline exists.

Prior work to survey includes seL4, Xen, MINIX 3, Barrelfish, Linux's device
and IOMMU models, and historical exokernels. The comparison asks which
problems each system solves, which guarantees it provides, and how Eloquence's
claim differs. The project does not copy a system's design and label it
original.

The Hardware Observatory will begin as a host-side parser/exporter for bounded
kernel event records. It can later collect CPU cycles/PMU data, page counts,
interrupts, DMA mappings, and device events. CSV/JSON exports include metadata
and loss counters so results remain interpretable.

## Milestones

Dates below are effort windows from the source proposal, not delivery promises.

| Milestone | Work | Exit evidence |
| --- | --- | --- |
| M0 — Foundation | Boot/CPU contracts, memory map, build matrix, research specification | Reproducible builds; boot observations recorded per supported target |
| M1 — Protected resources | User domains, syscall boundary, MMIO capability authorization and revocation | Actual user-mode allowed/denied/revoked run with audit trace |
| M2 — Memory and execution | Page ownership/freeing, user address-space lifecycle, timer and scheduler | Stress results, context-switch functional run, fault isolation |
| M3 — Device services | IRQ ownership, device manager, IPC, user-space driver prototype | Driver fault does not grant access beyond its capabilities |
| M4 — Storage and network | VirtIO block/network, initial filesystems and network stack | Repeatable functional scenarios and bounded-parser evidence |
| M5 — DMA research | DMA buffers and IOMMU domains | Authorized, denied, revoked and device-after-revoke outcomes on compatible hardware |
| M6 — Portability | AArch64, RV64, MIPS and one selected FPGA configuration | Comparable resource-lifecycle slice, per-target guarantees and boot logs |
| M7 — SDK and evaluation | Host tools, native examples, benchmark harness, technical report | Reproducible data, raw outputs, limitations, and reviewed related work |

The 12-month and 18-month outlines in the project proposal are planning
scenarios, not release commitments. Each milestone can span longer when its
evidence gate has not been met.

## Documentation and publication

Keep design decisions in ADRs with context, alternatives, consequences, and
revisit triggers. The public project can use GitHub Discussions and Projects
for community planning, API docs for code contracts, and separate research
reports for hypotheses and results. Doxygen/MkDocs and LaTeX are candidates
once the corresponding APIs and experiments exist.

Potential report topics are capability-based hardware ownership, resource
management in an experimental exokernel, user-level versus kernel-mediated
device operations, and isolation of device/DMA access. Publication is a later
outcome of evidence, not an implementation milestone by itself.

## Related project records

- [Hardware Ownership Research](hardware-ownership.md)
- [Multi-Architecture and FPGA Research Plan](multi-architecture-and-fpga.md)
- [ADR-0008: Multi-Architecture and FPGA Research Program](../adr/0008-multi-architecture-and-fpga-research.md)
- [ADR-0009: User-Mode Capability Boundary](../adr/0009-user-mode-capability-boundary.md)
