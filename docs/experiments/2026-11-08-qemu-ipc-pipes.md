# QEMU IPC Pipe Run

## Environment

- Target: x86-64 under QEMU `q35`, booting the Limine ISO (no disk required for
  this kernel-only scenario).
- Kernel: `make x86_64-objects` + `make iso`, built with
  `clang --target=x86_64-unknown-none-elf` under `-Wall -Wextra -Werror`.
- Build artifacts: `build/eloquence.iso`.

## Observations

- The kernel boots and reaches the serial monitor: memory map, page allocator,
  and PCI discovery all run unchanged.
- `make x86_64-objects`, `make userland-elf`, and `make iso` complete with zero
  warnings under `-Werror`.
- The new IPC pipe syscalls are registered in the x86-64 ABI:
  `IPC_PIPE_CREATE` (79), `IPC_PIPE_WRITE` (80), `IPC_PIPE_READ` (81),
  `IPC_PIPE_CLOSE` (82), all gated by `KERNEL_CAP_IPC_BIT`.
- Pipe implementation in `kernel/ipc/ipc.{c,h}`: bounded circular buffer
  (256 bytes), head/tail/count cursors, independent reader/writer reference
  counts, `ipc_pipe_create/write/read/close`.
- Waitqueue support in `kernel/process/waitqueue.{c,h}`: per-pipe read and
  write queues; blocked domains are marked `KERNEL_PROCESS_BLOCKED` and
  descheduled by the round-robin scheduler, then woken when the condition
  becomes satisfiable or the far end closes.
- Write blocks when the buffer is full; read blocks when empty. Closing one end
  wakes all domains blocked on the other end (EOF / broken pipe).

This verifies the build and boot baseline with the new pipe primitives present,
including blocking/waitqueue behavior. It does not yet exercise a runtime pipe
between two scheduled domains end-to-end, does not measure pipe latency, and
does not yet wire pipes into the shell's multi-process pipelines. Those remain
for later work.

## References

- [ADR-0010: Interprocess Pipes](../adr/0010-ipc-pipes.md)
- [Eloquence System Research Roadmap](../research/system-roadmap.md)
