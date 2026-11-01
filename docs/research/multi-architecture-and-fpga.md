# Multi-Architecture and FPGA Research Plan

**Status:** Architecture plan; ports are not yet implemented

Eloquence will test whether one explicit resource-ownership model can survive
across materially different privilege, memory, and device models. Portability
is an experiment: it must preserve stated guarantees while making each
architecture's differences visible.

## Target sequence

| Target | Initial machine | Boot direction | First research question |
| --- | --- | --- | --- |
| x86-64 | QEMU `q35` + EDU | Limine | Existing resource discovery, MMIO, and capability baseline |
| AArch64 | QEMU `virt`, pinned CPU model | Limine/UEFI | How exception levels, translation attributes, and GIC delivery affect resource grants |
| RV64 | QEMU `virt`, OpenSBI/SBI | Limine/firmware | How Sv39, `SFENCE.VMA`, SBI, and platform discovery affect isolation and transition cost |
| RV64 FPGA | CVA6 configuration with Sv39, named supported board | Board firmware/reset flow | Whether the same kernel mechanisms survive a real soft-core SoC and FPGA memory/peripheral integration |
| MIPS | QEMU Malta, named MIPS32 CPU model | Separate firmware/ELF entry path | Which parts of the resource model depend on modern 64-bit MMU and boot conventions |

The exact QEMU versions, CPU models, board revisions, firmware, and device
layout will be pinned before runtime comparisons. ARM's `virt` board is a
virtual platform, not a Raspberry Pi model. RISC-V `virt` is also explicitly a
virtual platform; the generated device tree describes its devices. Neither
QEMU target substitutes for named physical-board evidence.

## Kernel boundary

The policy layer should describe resources, owners, rights, and revocation. It
must not directly execute architecture instructions or assume a particular
page-table format. Architecture backends provide:

- exception and privilege transitions;
- virtual-address validation, page-table creation, and TLB invalidation;
- cacheability and ordering for device mappings;
- interrupt masking and delivery primitives;
- MMIO reads, writes, and mapping teardown.

Platform backends provide boot arguments, physical-memory discovery, device
tree/ACPI parsing, UART, timers, interrupt-controller configuration, and bus
enumeration. PCI configuration-space access is not an architecture-neutral
operation: x86 port I/O and ECAM differ from common ARM/RISC-V SoC discovery.

The first extracted code contract is `struct mmio_mapping` plus
`arch_mmio_read32`, `arch_mmio_write32`, and `arch_mmio_unmap`. The current x86
backend implements it. Capability policy now calls that contract instead of
performing volatile register accesses itself. Each future backend must specify
its device-memory attributes and ordering semantics before claiming this
interface is complete.

The CPU utility contract in `kernel/arch/irq.h` covers saving/restoring local
interrupt state and halting the CPU. Its x86-64, AArch64, RV64, and MIPS32r2
implementations are present, while the active image still selects only the
x86-64 implementation. This is an instruction-level foundation, not evidence
that those targets boot or that interrupt controllers are configured.

The physical page allocator accepts a generic region source and a
physical-to-virtual conversion callback. The current x86 Limine adapter maps
Limine regions into that contract. AArch64/RV64 can provide their own Limine
adapters; the MIPS boot path can provide a different memory map and direct-map
policy without changing allocator policy.

## AArch64

Use QEMU `virt` with an explicit AArch64 CPU. Initial bring-up needs a console
and must inspect the actual entry exception level. The first source slice
requests the Limine framebuffer, memory map, and DTB, then paints boot
diagnostics through a 32-bit RGB framebuffer. `make aarch64-objects` compiles
this path and the AArch64 interrupt primitives; it does not link or boot a
kernel image. This is not a PL011 driver: the next platform step is to discover
the UART through the DTB and map its physical range with Device attributes
before accessing it. Limine does not promise that arbitrary reserved MMIO is
available through the HHDM. Until then, a missing or unsupported framebuffer
causes an unobservable stop because there is no serial fallback.

The Limine protocol permits entry at EL1 or EL2 with VHE depending on the
firmware and bootloader path, so the kernel must not assume one fixed EL. The
diagnostic records the current EL when a compatible framebuffer is present.
Page-table code must state its selected translation granule and memory
attributes; the initial implementation should support a single declared
granule rather than silently assuming that all Arm machines use the same
layout. Interrupt work must use the platform's GIC description. A linked ELF
and repeatable boot observation are still required before this target can be
called bootable.

## RISC-V and FPGA

Use QEMU RISC-V `virt` for bring-up, with OpenSBI or the firmware path required
by the chosen boot protocol. The initial kernel profile is RV64, S-mode,
LP64/soft-float, and Sv39. It must use `SFENCE.VMA` after page-table changes,
and must discover MMIO addresses and interrupt routing from the device tree.
Timer/IPI calls cross the firmware boundary through discovered SBI extensions.
PLIC and AIA are distinct interrupt-controller configurations and must be
recorded separately.

For FPGA work, CVA6 is a candidate because its upstream project targets both
FPGA and ASIC, documents a 64-bit Sv39 configuration, and maintains FPGA
integration guidance. A second candidate is the Shakti C reference platform,
which QEMU documents as based on an Arty A7 100T board. Choose one board only
after documenting its supported firmware, RAM, UART, timer, and interrupt
controller. Keep the source RTL and generated bitstream provenance alongside
the experiment record; a successful QEMU boot does not validate the bitstream
or board.

## MIPS

Start with the QEMU Malta machine and a pinned MIPS CPU model. It has a
different reset and firmware path from the current Limine-based x86 kernel.
Choose and record endianness and ABI before sharing binary interfaces. Keep
MIPS exception entry, TLB management, cache maintenance, and MMIO ordering in
its own backend. The first MIPS milestone is an independently booting
diagnostic kernel; common resource-policy code is introduced only after that
milestone compiles against the shared interfaces.

## Comparison protocol

Each architecture experiment records:

- compiler, target triple, flags, and source revision;
- bootloader/firmware versions and machine description;
- CPU model, privilege level, address-translation mode, and page size;
- memory-map source and device-discovery source;
- MMIO attributes, ordering primitives, and revocation semantics;
- functional result and, later, latency distributions under named conditions.

Functional compatibility and performance are separate findings. A shared
source API does not imply equal isolation guarantees or costs across CPUs.
