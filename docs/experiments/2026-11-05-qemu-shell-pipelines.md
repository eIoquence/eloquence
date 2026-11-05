# QEMU Shell Pipeline Run

## Environment

- Target: x86-64 under QEMU `q35`, booting the Limine ISO with a generated
  64 MiB GPT/FAT32 VirtIO disk.
- Shell: statically linked Ring-3 `SHELL.ELF` from the disk fixture.
- Pipeline model: commands run sequentially in the shell; each stage consumes
  the previous stage's bounded 256-byte buffer.

## Observations

- `echo hello eloquence | grep eloquence` printed `hello eloquence`.
- `echo hello world | wc` printed `lines 1 words 2 bytes 12`.
- `uname | grep x86_64` printed `Eloquence x86_64`.
- The interactive shell returned to its prompt after each pipeline.
- `make userland-elf` and `make -j4` completed successfully before boot.

This verifies only shell-local buffering for `echo`, `uname`, `version`,
`grep`, and `wc`. It is not a kernel pipe, does not connect scheduled processes,
and cannot yet capture VFS or system-report output. Pipeline syntax supports at
most eight stages; quotes, redirection within pipelines, and script files are
not implemented.
