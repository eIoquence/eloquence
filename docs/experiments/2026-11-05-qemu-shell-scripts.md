# QEMU Shell Script Run

## Environment

- Target: x86-64 under QEMU `q35` with the generated FAT32 VirtIO disk.
- Shell: statically linked Ring-3 `SHELL.ELF`.
- File access: root-only 8.3 path through the scalar `FS_READ_CHUNK` syscall.

## Observation

At the shell prompt, the following commands were entered:

```text
echo echo script-ok > DEMO.TXT
script DEMO.TXT
```

The shell confirmed the file write, then read the file and printed `script-ok`.
The shell remained interactive. The kernel and shell compiled, and QEMU booted
to the user shell before this observation.

Scripts are limited to 2 KiB and each command line to the shell's 80-byte line
capacity. Supported commands are `echo`, `uname`, `version`, system reports,
and the shell-local pipelines. This is command-file execution, not a complete
language: variables, branching, loops, process pipelines, and nested scripts
are unsupported. File reads are root-only and use scalar chunks of up to three
bytes.
