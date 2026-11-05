# FAT32 Access

The storage layer can mount a FAT32 volume inside a GPT partition and access
its root and nested directories. It validates the boot-sector signature, sector and cluster
geometry, FAT size and capacity, volume bounds, and root cluster before use.
Directory traversal and FAT chains are bounded by the validated cluster count.

The monitor exposes:

```text
fs list DEVICE GPT_PARTITION
fs cat DEVICE GPT_PARTITION 8.3_NAME
run DEVICE GPT_PARTITION 8.3_ELF_NAME
```

Partition numbers are the one-based GPT entry numbers shown by `gpt list`.
Directory listing and file lookup use short 8.3 names. The ring-3 shell tracks
the current directory per process and supports `cd NAME`, `cd /A/B`, `cd ..`,
and `cd /`;
file reads, writes, creates, deletes, stats, and listings operate in that
directory. `fs cat` is limited to 4 KiB and replaces non-printable bytes before
writing to the serial console. `run` can pass a root file to the bounded static
x86-64 ELF loader. The ring-3 shell can create empty files and directories,
replace files up to 16 KiB, and delete files. Mutations require block-device
write and flush support, but are not journaled; interrupted metadata updates
may leave the volume inconsistent. Long file names, directory removal, and
direct paths such as `/A/B/FILE.TXT` are not implemented yet.

The built-in QEMU image provides `HELLO.TXT`, `HELLO.ELF`, and `SHELL.ELF`
samples. See the [storage runtime record](../experiments/2026-11-03-qemu-storage-fs.md)
and [user-shell run](../experiments/2026-11-03-qemu-userspace-shell.md).
