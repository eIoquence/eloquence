# Functional OS Path

**Status:** Active; serial monitor, 100 Hz clock, ring-3 capability demo, VirtIO block, GPT, and read-only FAT32 booted and exercised in QEMU

## Goal

Define “functional” as an observable boot into a responsive system that accepts
commands, reports its hardware and memory state, and can eventually launch an
isolated native program. Build success alone does not meet this definition; each
stage needs a repeatable runtime observation.

## Approaches considered

1. **Storage first:** implement VirtIO Block and a filesystem. This gives useful
   persistence, but depends on DMA-safe memory, virtqueues, and runtime testing.
2. **Architecture first:** bring AArch64 and RV64 to repeatable boots. This
   improves portability, but multiplies platform-specific debug work before the
   current x86-64 path has an interactive user interface.
3. **Vertical x86-64 slice:** add a bounded serial command monitor, then timer
   interrupts, scheduling, and a user-program loader. This makes the existing
   platform directly observable and supplies a stable base for storage and
   networking.

The project will follow the third approach first. The monitor will be a
diagnostic interface, not a substitute for user space. Its first commands are
`help`, `hwinfo`, `mem`, `disk list`, and `gpt list [device]`; input is bounded,
commands are read-only except `halt`, and no dynamic allocation is required.

## Milestones

1. Persistent serial monitor after boot diagnostics. **Implemented and booted.**
2. QEMU boot and command transcript, captured as the first runtime record.
   **Observed:** `help`, `hwinfo`, `mem`, `uptime`, `irq status`, `disk list`,
   and `gpt list` respond; EDU register checks and ring-3 capability scenarios
   pass. See [the run record](../experiments/2026-11-03-qemu-x86_64-first-boot.md).
3. PIT clock, remapped PIC, IRQ0 acknowledgement, and uptime command.
   **Implemented:** PIT at 100 Hz, PIC remapping, xAPIC virtual-wire delivery,
   IRQ0 acknowledgement, and live uptime confirmed in QEMU. APIC support is
   checked before reading its MSR; IMCR routing is selected where available,
   while QEMU q35 retained firmware routing and still delivered ticks. x2APIC
   is not yet supported.
4. Bounded static ELF loading from FAT32 and synchronous ring-3 launch/exit.
   **Partially implemented:** `HELLO.ELF` runs in ring 3 and returns status 42;
   loaded code/stack pages are reclaimed. Scheduler, dynamic linking, and page
   table teardown remain open.
5. VirtIO Block, primary/backup GPT handling, and read-only FAT32 access.
   **Partially implemented:** VirtIO legacy PCI block, primary GPT, FAT32 root
   listing, and bounded short-name file reads. Backup GPT recovery and all
   filesystem writes remain open.
6. IPC and isolated device services, followed by network and multi-architecture
   runtime work.

QEMU and related host tooling are introduced at milestone 2. Every milestone
keeps compile, boot, and functional claims separate and records known limits.
