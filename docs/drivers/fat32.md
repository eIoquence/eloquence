# Read-only FAT32 Access

The storage layer can mount a FAT32 volume inside a GPT partition and read its
root directory. It validates the boot-sector signature, sector and cluster
geometry, FAT size and capacity, volume bounds, and root cluster before use.
Directory traversal and FAT chains are bounded by the validated cluster count.

The monitor exposes:

```text
fs list DEVICE GPT_PARTITION
fs cat DEVICE GPT_PARTITION 8.3_NAME
run DEVICE GPT_PARTITION 8.3_ELF_NAME
```

Partition numbers are the one-based GPT entry numbers shown by `gpt list`.
Directory listing and file lookup currently use short 8.3 names in the root
directory. `fs cat` is limited to 4 KiB and replaces non-printable bytes before
writing to the serial console. `run` can pass a root file to the bounded static
x86-64 ELF loader. There is no FAT write support, long-file-name handling,
subdirectory traversal, or crash-recovery mechanism.

The built-in QEMU image provides a reproducible `HELLO.TXT` sample. See the
[storage runtime record](../experiments/2026-11-03-qemu-storage-fs.md).
