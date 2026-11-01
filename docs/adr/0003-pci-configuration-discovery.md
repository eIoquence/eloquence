# ADR-0003: Initial PCI Configuration Discovery

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The first device-facing experiment needs stable PCI function identities and an inventory of BAR fields before resource ownership or MMIO mappings can be designed. The initial target is x86-64 QEMU, running with interrupts disabled and a single active CPU.

## Decision

1. Use x86 PCI Configuration Mechanism #1 through I/O ports `0xCF8` and `0xCFC` for initial discovery.
2. Scan all 256 bus numbers, 32 device numbers per bus, and up to 8 functions when function 0 marks a device as multifunction.
3. Read standard configuration fields and decode BAR base/type/prefetchable fields for type 0, 1, and 2 headers.
4. Do not write device configuration registers, probe BAR sizes, enable bus mastering, or map BARs in this milestone.
5. Treat each reported BAR as an observation only. It does not yet provide a validated extent or an ownership capability.

## Consequences

- Discovery needs no page-table changes and can precede the MMIO mapping design.
- Mechanism #1 exposes only the conventional 256-byte PCI configuration space; PCIe extended configuration through ACPI MCFG/ECAM is deferred.
- BAR sizing, resource conflict checks, interrupt routing, and MMIO access remain future milestones.
- Reads and scans are not safe for concurrent callers; serialization must be added before SMP or interrupt-driven PCI access.

## Revisit when

The project needs PCIe extended configuration space, validated BAR extents, or device resource assignment beyond QEMU's initial firmware-provided configuration.
