# ADR-0006: MMIO Capability Lifecycle

- **Status:** Accepted
- **Date:** 2026-11-01
- **Decision owners:** Eloquence maintainers

## Context

The EDU MMIO mapping can now be created and removed, but the research question needs an explicit authorization boundary around access. The current kernel has no user mode, process scheduler, or system-call boundary yet.

## Decision

1. Represent an MMIO grant as a bounded table entry containing an owner domain ID, read/write 32-bit rights, an owned mapping handle, and a generation number.
2. Transfer the mapping handle into the capability table when a grant succeeds.
3. Resolve every access by slot and generation, require the trusted caller's domain ID to match the owner, check the requested right, require 4-byte alignment, and verify the complete 32-bit access lies within the granted range.
4. Revoke by unmapping the range, invalidating the slot generation, and clearing its rights and owner.
5. Record grant, access, denial, and revoke outcomes in a fixed-size kernel audit ring.
6. Keep this interface kernel-internal. For now, the boot harness passes synthetic domain IDs; a future syscall layer must derive the actor ID from kernel-owned current-domain state rather than a user-provided argument.

## Consequences

- The first functional slice can exercise authorized access, foreign-domain and read-only-rights denial, revoke, stale-handle denial, and audit ordering without adding user mode.
- This is a software authorization test inside trusted kernel code; it does not yet prove memory isolation or unforgeability against hostile ring-3 code.
- The table and audit ring are fixed-size, single-CPU data structures. Delegation, subrange grants, variable access widths, and concurrency are deferred.
- A revoked mapping's virtual address is removed and its TLB entry invalidated. Physical page-table pages used to create the mapping remain allocated because the bootstrap allocator has no free operation.

## Revisit when

The kernel adds a user-mode domain, syscall entry, delegation, interrupt-driven device access, or SMP.
