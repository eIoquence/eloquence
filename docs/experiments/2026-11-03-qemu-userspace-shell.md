# QEMU User-Space Shell Run

**Date:** 2026-11-03  
**Branch:** `work`  
**Host:** Darwin arm64  
**Emulator:** QEMU 11.1.3 (TCG)  
**Kernel:** x86-64 Limine image with the polling VirtIO block driver

The session used `make run`. The generated 64 MiB GPT/FAT32 image contained
`SHELL.ELF`, a 1,392-byte static x86-64 user program compiled from
`userland/shell.c` without a C runtime. The kernel launched it automatically
from disk 0, GPT partition 1 after boot-time hardware discovery.

## Observed results

- The kernel booted, reported the QEMU VirtIO block device, read the primary
  GPT partition, and entered the interactive ring-3 shell.
- `fs list 0 1` displayed `HELLO.TXT` (76 bytes), `HELLO.ELF` (179 bytes), and
  `SHELL.ELF` (1,392 bytes).
- Boot automatically entered ring 3 and printed the user shell banner and
  `user>` prompt.
- The shell accepted `help` and printed its built-in command list.
- `echo ring3 works` printed the expected text from ring 3.
- `exit` returned status 0 and restored the kernel monitor prompt; a subsequent
  `fs list 0 1` still listed all three FAT32 fixture files.
- The shell remained responsive while the PIT timer ran. After waiting in the
  shell, the monitor reported `Uptime: 16 seconds (1639 timer ticks)`.
- Boot-time EDU capability checks reported `pass failures=0`.

## Limits

The user shell is a foreground process with no scheduler or child-process
launch. It implements `help`, `about`, `echo TEXT`, and `exit`, with Backspace
and Ctrl-U line editing. Buffered I/O and a stable syscall ABI are absent. If
no block device or `SHELL.ELF` is available, the kernel falls back to its
diagnostic monitor. Serial input uses a non-blocking syscall and user-mode
polling with `pause`. FAT32 remains read-only, and this run used QEMU TCG rather
than physical hardware.

## Shell 0.1 follow-up

The ring-3 shell now exposes `help`, `clear`, `echo`, `uname`, `uptime`,
`version`, `sysinfo`, `meminfo`, `cpuinfo`, `devinfo`, `irqinfo`, `bootinfo`,
`about`, and `exit`. QEMU interaction confirmed the new commands:
`uptime` reported live 100 Hz timer ticks, `clear` emitted the terminal
clear/home sequence, `echo` preserved multiword arguments, and all six system
reports returned kernel-sourced data. `meminfo` labels its value as total
usable physical memory and explicitly does not claim it is free memory. The
generated QEMU FAT32 fixture is refreshed by each `make run` so shell changes
are not silently tested against a stale disk image. Unique built-in prefixes
complete with Tab, and the Up arrow recalls the previous non-empty command;
both behaviors were exercised in QEMU. `ls`, `cat HELLO.TXT`, and `pwd` also
worked through kernel-mediated, read-only FAT32 access. `cat` accepts bounded
8.3 names, and `stat` reports root entry type and size. These operations now
pass through a single-root VFS adapter backed by FAT32. `pwd` works at `/`;
`cd` only confirms the root because there are no subdirectories in the shell
interface yet. Multiple mounts and write support are absent.
