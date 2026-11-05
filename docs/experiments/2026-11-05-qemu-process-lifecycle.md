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

The kernel keeps the 16 most recent process records. PID allocation is
monotonic within a boot. This demonstrates bookkeeping, self-exit, and
synchronous nested `exec`, not a scheduler: only one user process runs at a
time, and timer interrupts do not switch address spaces. `wait`, background
jobs, and termination of another process remain unimplemented.
