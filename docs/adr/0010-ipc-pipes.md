# ADR-0010: Interprocess Pipes

- **Status:** Accepted for experimental prototype
- **Date:** 2026-11-08
- **Decision owners:** Eloquence maintainers

## Context

The IPC subsystem provides named shared memory, semaphores, signals, and bounded
message queues, but no byte-stream channel between two separately scheduled
domains. The shell documents "buffered single-process pipelines" that
explicitly "do not connect separate scheduled processes." The roadmap lists
kernel IPC pipes as future work under milestones M2/M3.

A pipe primitive is needed before user-space drivers and services can exchange
streams without sharing memory, and before the shell can support true
multi-process pipelines.

## Decision

1. Add a bounded, named pipe type to `kernel/ipc/ipc.{c,h}` alongside shared
   memory, semaphores, signals, and message queues. Each pipe holds a circular
   byte buffer (`IPC_PIPE_BUFFER_SIZE`, 256 bytes), head/tail/count cursors,
   and independent reader/writer reference counts.
2. Expose four functions: `ipc_pipe_create`, `ipc_pipe_write`, `ipc_pipe_read`,
   `ipc_pipe_close`. Pipe names are bounded to `IPC_MAX_NAME` (16 bytes).
3. Expose four syscalls on x86-64, gated by `KERNEL_CAP_IPC_BIT` (the same
   capability used by `IPC_CREATE`/`IPC_SEND`/`IPC_RECEIVE`):
   - `IPC_PIPE_CREATE` (79): named pipe creation, returns pipe id.
   - `IPC_PIPE_WRITE` (80): bounded register-carried write (up to 8 bytes per
     call), returns bytes written.
   - `IPC_PIPE_READ` (81): bounded read into a caller buffer, returns bytes
     read.
   - `IPC_PIPE_CLOSE` (82): close one end (read or write); the pipe is freed
     when both ends reach zero references.
4. `ipc_pipe_write` blocks the calling domain when the buffer is full;
   `ipc_pipe_read` blocks the calling domain when the buffer is empty. Blocking
   is implemented through a per-pipe waitqueue pair (read queue / write queue)
   in `kernel/process/waitqueue.{c,h}`: a blocked domain is marked
   `KERNEL_PROCESS_BLOCKED` and the scheduler switches to another runnable
   domain; when the condition becomes satisfiable (data written / space freed /
   far end closed), the woken domain is marked runnable again. Closing one end
   wakes all domains blocked on the other end so they can observe EOF / broken
   pipe rather than blocking forever.
5. Pipes are kernel-mediated: data is copied through the kernel buffer. No user
   pointers cross the ABI for pipe payloads (consistent with the existing
   register-carried IPC ABI).

## Consequences

- Domains can exchange byte streams without shared memory, with the kernel
  mediating each write/read and enforcing the `KERNEL_CAP_IPC_BIT` gate.
- Blocking with waitqueue-based wakeup is implemented, so domains are descheduled
  while waiting on an empty/full pipe instead of busy-polling. This provides
  basic backpressure between pipe endpoints.
- A pipe with one end closed wakes all blocked domains on the other end; there is
  no distinct EOF/error code yet beyond reference counting.
- Write granularity is limited to 8 bytes per syscall because payload travels in
  a register, matching the existing `IPC_MQ_SEND` convention. Larger streams
  require multiple calls.
- Pipes do not yet provide unforgeable handles, capability transfer between
  domains, or shell multi-process pipeline wiring. Those are deferred to later
  IPC work.

## QEMU verification observed

Build-only verification completed: `make x86_64-objects`, `make userland-elf`,
and `make iso` all succeed under `-Werror`; the x86-64 ISO boots under QEMU
(memory map, page allocation, PCI discovery all pipe through unchanged). A
dedicated runtime pipe scenario is tracked in the companion experiment record.

## References

- [Eloquence System Research Roadmap](../research/system-roadmap.md)
- [ADR-0009: User-Mode Capability Boundary](0009-user-mode-capability-boundary.md)

## Revisit when

Revisit when adding blocked-task wakeup / waitqueues, handle hardening, pipe
capability transfer, or shell multi-process pipeline wiring.
