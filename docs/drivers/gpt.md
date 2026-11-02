# Read-only GPT Discovery

**Status:** x86-64 source prototype; not boot-validated

`kernel/storage/gpt.c` reads the primary GPT header at LBA 1 through the
bounded block-device API. It checks the header revision, size, location,
header CRC32, entry-array CRC32, arithmetic bounds, usable LBA range, nonzero
partition GUIDs, and overlapping partition extents before reporting partitions.
The parser handles only sector sizes from 512 through 4096 bytes, caps the
entry array at 256 KiB and 4,096 entries, and reports at most 128 partitions.
It uses a fixed scratch buffer, requires serialized calls, and performs no
writes. Kernel startup calls it after the legacy ATA probe; this behavior has
not been observed on a booted device.

The parser does not inspect or recover from the backup GPT, validate the
protective MBR, decode partition names, or mount filesystems. CRC checks protect
against accidental corruption; they do not authenticate disk metadata. The
on-disk layout and CRC fields follow [UEFI Specification 2.11, Chapter 5](https://uefi.org/specs/UEFI/2.11/05_GUID_Partition_Table_Format.html).
