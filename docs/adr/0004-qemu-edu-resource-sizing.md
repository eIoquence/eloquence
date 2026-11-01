# ADR-0004: QEMU EDU Resource Sizing

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

PCI discovery provides BAR type and base fields but not resource length. The first MMIO experiment needs a deterministic device and a validated resource extent. QEMU's EDU device exposes a documented 1 MiB memory BAR and a fixed vendor/device ID.

## Decision

1. Add QEMU EDU (`1234:11e8`) to the project's QEMU run configuration.
2. Keep the general PCI inventory read-only.
3. Probe only EDU BAR0. Save the command and BAR registers, disable I/O and memory decoding, write all ones to the BAR register(s), read the device's size mask, and restore the saved BAR and command values.
4. Reject unsupported BAR types, absent masks, and non-power-of-two sizes.
5. Do not enable bus mastering, map the BAR, or expose it outside the kernel in this milestone.

## Consequences

- The experiment has a documented expected size to compare against the probed result.
- BAR probing is a configuration write transaction and must remain limited to a known, inactive device until resource ownership and driver coordination exist.
- The current operation assumes one active CPU and masks local interrupts while accessing PCI configuration space. SMP-safe serialization remains future work.
- The measured size is still only a resource description; it does not prove that the BAR's physical pages can be mapped with the required cache policy.

## Revisit when

The experiment moves to other PCI devices, multiple CPUs, hotplug, or a platform that assigns resources differently from QEMU EDU.
