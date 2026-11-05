# QEMU Nested FAT32 Run

## Environment

- Target: x86-64 under QEMU `q35` with the generated GPT/FAT32 VirtIO disk.
- Shell: Ring-3 `SHELL.ELF`; operations use the mounted writable FAT32 volume.

## Observations

The following sequence was entered interactively:

```text
mkdir ALPHA
cd ALPHA
pwd
echo nested-data > NOTE.TXT
cat NOTE.TXT
stat NOTE.TXT
mkdir BETA
cd BETA
pwd
cd ..
pwd
cd /
pwd
ls
cd ALPHA
ls
rm NOTE.TXT
ls
```

The shell reported `/ALPHA`, then `/ALPHA/BETA`, and returned to `/ALPHA` via
`cd ..`. It wrote and read `NOTE.TXT`, listed its size (12 bytes), showed
`BETA` in the parent, removed the file, and then listed only `BETA`. Returning
to `/` showed `ALPHA` in the root listing.

A separate run created `/A/B` and used `cd /A/B` directly. `pwd` reported
`/A/B`; `echo path-ok > X.TXT` and `cat X.TXT` confirmed that file operations
used the nested current directory. `cd /A` returned to the intermediate
directory, and `cd ..` then returned to `/`.

Directory handles are FAT32 cluster IDs tracked per process by the kernel.
Names remain 8.3. A second boot run confirmed that `cd /A/B` traverses a
multi-component directory path and that file operations in `/A/B` use that
working directory. Direct multi-component file arguments, directory removal,
journal/recovery, and multi-volume mounts remain unsupported.
