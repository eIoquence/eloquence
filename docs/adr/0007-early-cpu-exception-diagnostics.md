# ADR-0007: Early CPU Exception Diagnostics

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

MMIO experiments and page-table changes can cause CPU exceptions. Without an IDT, an early fault may reset the virtual machine without a useful serial trace.

## Decision

1. Install interrupt gates for x86 exception vectors 0 through 31 after serial initialization.
2. Route installed vectors to a common handler that reports the vector, terminates a ring-3 demo domain on user exceptions, and halts on kernel exceptions.
3. For page faults, also report CR2, the faulting linear address.
4. Keep external interrupts disabled. Terminate the current demo domain on a
   user exception; kernel exceptions remain fatal.

## Consequences

- Expected early faults produce a serial diagnosis instead of relying on a reboot to infer what happened.
- The handler does not report error codes or saved instruction pointers. Kernel exceptions have no dedicated interrupt stack. Double faults caused by a corrupted stack may still prevent useful output.
- IDT handling is diagnostic only; timer, device, and inter-processor interrupts remain future work.

## Revisit when

The kernel adds nested-exception recovery, IST stacks, or enables hardware interrupts.
