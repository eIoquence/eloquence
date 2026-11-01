# Experimental x86-64 Syscall ABI

**Status:** Prototype; not a stable application ABI

This ABI exists to test a real privilege boundary for the initial MMIO
capability experiment. It is single-CPU, synchronous, and intentionally accepts
only scalar values. It has no user pointers, scheduling, IPC, or compatibility
guarantee.

## Register contract

User code enters with the `SYSCALL` instruction:

| Register | Input |
| --- | --- |
| `RAX` | Syscall number |
| `RDI` | Argument 0 |
| `RSI` | Argument 1 |
| `RDX` | Argument 2 |

`RCX` and `R11` are clobbered by the architectural transition. The kernel
derives the actor domain from kernel-owned current-domain state; no syscall
accepts a domain ID.

## Calls

| Number | Name | Arguments | Result |
| --- | --- | --- | --- |
| 0 | `EXIT` | `RDI`: status | Does not return; resumes the kernel caller |
| 1 | `MMIO_READ32` | `RDI`: handle, `RSI`: byte offset | `RAX[63:32]`: result code; `RAX[31:0]`: value on success |
| 2 | `MMIO_WRITE32` | `RDI`: handle, `RSI`: byte offset, `RDX[31:0]`: value | `RAX[31:0]`: result code |
| 3 | `MMIO_REVOKE` | `RDI`: handle | `RAX[31:0]`: result code |

For calls 1–3, result codes are the `mmio_capability_result` values declared in
`kernel/capability/mmio_capability.h`. An invalid syscall number returns
`MMIO_CAPABILITY_INVALID_ARGUMENT` in the high half of `RAX`.

The handle encoding is `generation[63:32]`, reserved zero bits `[31:8]`, and
slot `[7:0]`. Invalid reserved bits are rejected. Handles are currently
predictable slot/generation values; this ABI does not provide unforgeability.

The ring-3 demonstration also exercises an unknown syscall number and a handle
with nonzero reserved bits. Those are intended runtime checks; source presence
and compilation do not show that the checks have passed.

`EXIT` uses an internal all-ones return sentinel and accepts the full 64-bit
status range. The kernel reports a user exception separately from the status
value and records the exception vector as its fault status. A user exit status
can therefore never be confused with fault termination. This is an internal
prototype result contract, not a process ABI.

## Protection boundary

The demo maps code user-readable/executable and stack user-readable/writable
and non-executable. Kernel high-half mappings remain supervisor-only. Syscalls
do not copy or dereference user pointers. MMIO stays kernel-mapped and is
accessed only after capability ownership, rights, alignment, and bounds checks.
External interrupts remain disabled while the demo runs.

The implementation supports one active domain on one CPU. A scheduler,
address-space teardown, frame reclamation, handle hardening, asynchronous
interrupts, and a stable syscall versioning scheme are future work. Build
success is not a runtime isolation result; the ring-3 scenarios still need an
observed boot run.
