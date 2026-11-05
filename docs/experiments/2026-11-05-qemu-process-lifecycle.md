# QEMU Process Lifecycle Prototype

**Date:** 2026-11-05  
**Branch:** `work`  
**Machine:** x86-64 QEMU TCG (`q35`, 512 MiB)  
**Workload:** `SHELL.ELF` from the generated FAT32 fixture

## Observed behavior

- The shell started as PID 3 and `ps` reported it as `running`.
- `kill 999` was rejected and the shell remained responsive.
- `kill 3` terminated the current shell with status 137 and returned to the
  kernel monitor.
- The monitor launched `SHELL.ELF` again as PID 4. The new shell's `ps` output
  showed PID 3 as `exited` with status 137 and PID 4 as `running`.
- `kill 4` terminated PID 4 and returned to the monitor.
- From the restarted parent shell, `exec HELLO.ELF` printed the child's
  message and returned status 42 while the parent shell remained responsive.
- `exec SHELL.ELF` started a nested shell as PID 5. While PID 5 was active,
  its `ps` showed PID 3 blocked, PID 4 exited, PID 5 running, and nested
  `HELLO.ELF` as PID 6 with PPID 5. Exiting PID 5 restored PID 3; the parent's
  next `ps` showed it running and both child records exited.
- `spawn HELLO.ELF` returned PID 4 while the parent shell continued. The child
  printed its message and exited with status 42; `ps` retained its exit record.
- `spawn SPIN.ELF` launched a CPU-bound infinite loop as PID 5. The timer
  preempted it and resumed the interactive shell; `ps` showed PID 5 runnable
  with increasing runtime ticks while PID 3 remained running. `kill 5` stopped
  and reaped it with status 137, and the shell remained usable. A second run
  confirmed successful `kill` returns quietly and the PID appears exited.
- Nested synchronous `exec HELLO.ELF` also returned correctly while the timer
  scheduler was active.
- Exiting the root shell while a spawned worker remained active returned to
  the kernel monitor; the scheduler marked and reaped the remaining task.

The kernel keeps the 16 most recent process records. PID allocation is
monotonic within a boot. The 100 Hz x86-64 timer saves the user register and
iret frame, rotates runnable processes, switches CR3 and the per-process
kernel stack, then restores the selected context. `wait`, pipes, and IPC remain
unimplemented. The scheduler is single-CPU and fixed-capacity; the initial
root process owns its spawned task tree and tears it down on exit. Runtime
behavior was verified only under this QEMU TCG configuration.
