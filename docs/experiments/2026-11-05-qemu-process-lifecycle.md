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

The kernel keeps the 16 most recent process records. PID allocation is
monotonic within a boot. This demonstrates process bookkeeping and self-exit,
not a scheduler: only one user process runs at a time, and timer interrupts do
not switch address spaces. `exec`, `wait`, background jobs, and termination of
another process remain unimplemented.
