# ADR-0009: User-Mode Capability Boundary

- **Status:** Accepted for experimental prototype
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The prior MMIO exercise called capability functions directly from trusted
kernel code and supplied actor IDs as arguments. That checked bounds and
rights, but did not test protection against an untrusted caller. The project
needs a real privilege boundary before it can claim that capability checks
protect device access.

## Decision

1. Start with a single-CPU x86-64 ring-3 domain and the architectural
   `SYSCALL`/`SYSRET` path. Install explicit user/kernel code and data
   descriptors and a TSS kernel stack before entering user mode.
2. Keep the current-domain identity in kernel-only state. Syscall numbers,
   scalar capability handles, offsets, and values cross the ABI; the caller
   cannot provide an actor/domain ID or a user pointer in this first ABI.
3. Change the MMIO capability read, write, and revoke APIs to derive the actor
   from kernel state. Grant and privileged cleanup remain kernel operations.
4. Map the demo's code read-only/executable and its stack writable/non-
   executable. Keep the kernel's high-half mappings supervisor-only in the
   user address space.
5. Exercise owner read/write and register restoration, read-only rights,
   foreign-domain denial, revoke, stale/malformed-handle denial, invalid
   syscalls, user-fault termination, and audit records from actual ring-3 code
   using the QEMU EDU MMIO region.
6. Keep external interrupts disabled in user mode until a platform interrupt
   controller and timer path are initialized.

## Consequences

- The first experiment demonstrates a real privilege transition and
  kernel-derived actor identity on one CPU. It does not provide scheduling,
  domain creation/destruction, IPC, interrupt-driven I/O, or fault recovery
  beyond terminating the single demo domain.
- The first syscall ABI avoids user pointers, reducing pointer-validation
  exposure while the VM lifecycle is incomplete.
- Capability handles remain predictable slot/generation values. This prototype
  does not claim unforgeability, resistance to guessed same-domain handles,
  or cryptographic entropy. Those require a separate capability-table design.
- The page allocator cannot free frames or page-table pages. User address-space
  teardown and resource reclamation remain incomplete.
- User exceptions return to the kernel continuation and terminate the current
  demo domain. This path has only compile-time review so far; nested faults,
  NMI, machine-check, and corrupted kernel state are not isolated or recovered.
- The implementation must compile and link before it is considered buildable;
  a repeatable boot observation is still required before claiming runtime
  behavior.

## Verification gate

The next runtime experiment must show that the read-only device capability
works for its owner, fails for another ring-3 domain, rejects writes, stops
working after revoke, and produces an audit trail. It must also demonstrate
that attempts to execute kernel code or read/write kernel mappings fault in
user mode. Build-only CI does not satisfy this gate.

## References

- [Intel 64 and IA-32 Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Eloquence System Research Roadmap](../research/system-roadmap.md)
- [ADR-0006: MMIO Capability Lifecycle](0006-mmio-capability-lifecycle.md)

## Revisit when

Revisit after the user-mode experiment has been boot-validated or when adding
SMP, interrupt delivery to user domains, a scheduler, capability delegation,
or user-memory syscall arguments.
