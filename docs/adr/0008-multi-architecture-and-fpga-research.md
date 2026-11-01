# ADR-0008: Multi-Architecture and FPGA Research Program

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The current prototype is tied to x86-64 in its boot request, exception setup,
serial output, PCI configuration mechanism, and page-table implementation.
Adding more target names to the build would not make those mechanisms portable.
The research direction now includes ARM64, RISC-V, MIPS, and FPGA-hosted
processors, so the kernel needs explicit architecture and platform boundaries.

Limine provides protocol support for x86-64, AArch64, and RISC-V 64-bit, but
not MIPS. QEMU provides system emulation for all four families. The RISC-V
ecosystem also has application-class open-source processor implementations
that target FPGA and ASIC, including CVA6.

## Decision

1. Keep x86-64 as the first functional baseline while moving shared resource
   policy behind architecture-neutral interfaces.
2. Treat CPU mechanisms and board/platform mechanisms as separate backends.
   CPU backends own exception entry, privilege transitions, address
   translation, TLB maintenance, interrupt masking, and MMIO ordering. Platform
   backends own boot handoff, memory/device discovery, UART, interrupt
   controller, timers, and bus-specific discovery.
3. Bring up AArch64 and RV64 as the first new architectures using Limine where
   the firmware path permits, and QEMU `virt` as their reproducible platform.
   AArch64 requires an explicit CPU model; its `virt` platform includes PL011
   UART and GIC devices. RV64 starts in supervisor mode through firmware/SBI,
   with Sv39 as the initial virtual-memory target and the platform device tree
   as the source of device addresses and interrupts.
4. Evaluate an FPGA target after the RV64 QEMU bring-up. The first candidate is
   an upstream-documented CVA6 64-bit configuration with Sv39 on an explicitly
   supported board. Every experiment records the RTL revision, configuration,
   FPGA part and board, bitstream hash, clock, memory subsystem, and peripheral
   map.
5. Treat MIPS as a separate experimental port with a separately documented
   boot path and ABI. Start with a QEMU Malta configuration; do not assume the
   Limine handoff or the common 64-bit ABI applies to it.
6. Do not claim architecture support until the target has a buildable image,
   a documented machine contract, and a repeatable boot observation. Do not
   infer physical-hardware performance from QEMU.

## Consequences

- The capability and resource-lifecycle policy can be shared, while address
  translation, cacheability, memory ordering, interrupts, and device discovery
  remain explicit in each backend.
- The first code boundary is architecture-owned MMIO access and mapping
  lifecycle. Further shared APIs should be extracted only when a second backend
  can implement and validate them.
- AArch64 SoC diversity means the architecture port cannot by itself promise
  support for Raspberry Pi or arbitrary ARM boards. Each board needs its own
  boot and device-discovery contract.
- RISC-V is the primary open-ISA/FPGA research track. MIPS adds a useful
  comparative architecture but remains a distinct experimental target.
- A target can be listed as planned or experimental without being presented as
  implemented or supported.

## References

- [Limine supported architectures and boot protocols](https://github.com/Limine-Bootloader/Limine)
- [Limine boot protocol specification](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md)
- [QEMU AArch64 `virt` platform](https://www.qemu.org/docs/master/system/arm/virt.html)
- [RISC-V supervisor ISA specification](https://github.com/riscv/riscv-isa-manual/blob/main/src/priv/supervisor.adoc)
- [RISC-V SBI specification](https://docs.riscv.org/reference/sbi/intro.html)
- [QEMU RISC-V `virt` platform](https://www.qemu.org/docs/master/system/riscv/virt.html)
- [CVA6 FPGA and ASIC project documentation](https://github.com/openhwfoundation/cva6/blob/master/docs/index.rst)
- [QEMU MIPS system emulator](https://www.qemu.org/docs/master/system/target-mips.html)

## Revisit when

Revisit after the AArch64 and RV64 backends boot and implement the same
resource-lifecycle slice, or when an FPGA/MIPS experiment demonstrates that a
shared interface hides a hardware behavior relevant to the research question.
