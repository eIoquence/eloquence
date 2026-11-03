# First x86-64 QEMU Runtime Record

**Date:** 2026-11-03  
**Branch:** `work`  
**Host:** Darwin 27.0.0 arm64  
**Emulator:** QEMU 11.1.3 (TCG)  
**Compiler/linker:** Apple Clang 21.0.0, Homebrew LLD 23.1.4  
**ISO build tool:** xorriso 1.5.8.pl02

## Configuration

The ISO was built with `make iso` and booted with the project target:

```sh
qemu-system-x86_64 -machine q35 -m 512M -cdrom build/eloquence.iso \
  -boot d -display none -device edu -serial stdio -monitor none \
  -no-reboot -no-shutdown
```

The image contained the x86-64 kernel ELF whose SHA-256 was
`ab77d9362b0b5ca4c9bc5b816f8eadb672cb3452cfd8974823092773753eeba6`.
No disk image was attached, so the block-device registry correctly reported
zero devices.

## Observed results

- Limine 12.9.6 booted the kernel and reported the physical memory map.
- PCI enumeration found seven functions and identified the QEMU EDU device.
- EDU BAR0 identity and liveness inversion/restore checks passed.
- The ring-3 MMIO capability scenario passed with failure mask `0`:
  owner read/write, read-only denial, foreign-domain denial, revocation,
  stale/malformed handle rejection, invalid syscall rejection, and user-fault
  termination were observed. The invalid-opcode message was the deliberate
  fault-isolation case.
- The 100 Hz PIT reached IRQ0 through the remapped PIC and local APIC
  virtual-wire path. QEMU q35 did not expose the optional IMCR, so the kernel
  retained firmware PIC routing; observed ticks confirmed delivery. `irq
  status` reported IF enabled, master PIC mask `0xfe`, no pending/in-service
  PIC interrupt, and local APIC enabled.
- The serial monitor responded to `help`, `hwinfo`, `mem`, `uptime`,
  `irq status`, `disk list`, and `gpt list`. One sample showed 1681 timer
  ticks after about 17 seconds.

## Limits

This is a QEMU functional observation, not physical hardware validation. The
kernel still has no scheduler, independent user process lifecycle, ELF loader,
filesystem, or user-space shell. The timer path uses xAPIC virtual-wire mode;
x2APIC and other interrupt sources are not yet supported. Capability handles
are predictable slot/generation values and are not claimed to be unforgeable.
