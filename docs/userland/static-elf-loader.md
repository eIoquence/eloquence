# Static x86-64 ELF Loader

The monitor can load a small static ELF64 executable from the root of a FAT32
GPT partition and run it synchronously in ring 3:

```text
run DEVICE GPT_PARTITION 8.3_NAME
```

During boot, the kernel tries `SHELL.ELF` on block device 0, GPT partition 1.
On a successful launch, `exit` returns to the diagnostic monitor. Missing or
invalid shell files leave the kernel monitor available.

The loader accepts little-endian x86-64 `ET_EXEC` images with bounded program
headers and at most 16 mapped pages, including a two-page user stack. It rejects
dynamic/interpreter segments, writable executable segments, out-of-range memory
regions, malformed offsets, and non-executable entry points. It maps each
segment using its ELF read/write/execute flags and invokes the existing `EXIT`
syscall to return a status. CPU exceptions are reported as process faults.

The image reader is capped at 4 KiB, the process is single-threaded and
synchronous, and the syscall ABI is still experimental. Address-space page
tables remain allocated for reuse; loaded code and stack frames are unmapped and
reclaimed after exit. There is no scheduler, dynamic linker, argument vector,
or general-purpose process management yet. User programs can read one serial
character non-blockingly and emit individual validated ASCII characters
through the experimental ABI.

The QEMU disk fixture includes `HELLO.ELF`, which prints a short greeting and
exits with status 42, and `SHELL.ELF`, a minimal interactive ring-3 shell. See
the [ELF process run](../experiments/2026-11-03-qemu-elf-process.md) and
[user-shell run](../experiments/2026-11-03-qemu-userspace-shell.md).
