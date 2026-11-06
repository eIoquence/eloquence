#!/usr/bin/env python3
"""Create a disposable GPT/FAT32 disk image for the QEMU run target."""

import argparse
import math
import struct
import uuid
import zlib
from pathlib import Path

SECTOR_SIZE = 512
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ARRAY_SECTORS = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE // SECTOR_SIZE
PARTITION_START = 2048
FAT_RESERVED_SECTORS = 32
FAT_COUNT = 2
ELF64_HEADER_SIZE = 64
ELF64_PROGRAM_HEADER_SIZE = 56
HELLO_TEXT = (
    b"Welcome to Eloquence.\r\n"
    b"VirtIO block and FAT32 read-only access are online.\r\n"
)
CONTROL_SCRIPT = (
    b"set RESULT=pass\n"
    b"if $RESULT=pass\n"
    b"echo conditional-pass\n"
    b"else\n"
    b"echo conditional-fail\n"
    b"endif\n"
    b"if $RESULT=fail\n"
    b"echo wrong-branch\n"
    b"else\n"
    b"echo else-pass\n"
    b"endif\n"
    b"repeat 3\n"
    b"echo loop-pass\n"
    b"endrepeat\n"
)


def put_u16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def put_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def make_gpt_header(
    current_lba: int,
    backup_lba: int,
    first_usable: int,
    last_usable: int,
    entries_lba: int,
    disk_guid: uuid.UUID,
    entries_crc: int,
) -> bytes:
    header = bytearray(SECTOR_SIZE)
    header[:8] = b"EFI PART"
    put_u32(header, 8, 0x00010000)
    put_u32(header, 12, 92)
    put_u64(header, 24, current_lba)
    put_u64(header, 32, backup_lba)
    put_u64(header, 40, first_usable)
    put_u64(header, 48, last_usable)
    header[56:72] = disk_guid.bytes_le
    put_u64(header, 72, entries_lba)
    put_u32(header, 80, GPT_ENTRY_COUNT)
    put_u32(header, 84, GPT_ENTRY_SIZE)
    put_u32(header, 88, entries_crc)
    put_u32(header, 16, zlib.crc32(header[:92]) & 0xFFFFFFFF)
    return bytes(header)


def put_u64(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buffer, offset, value)


def make_hello_elf() -> bytes:
    code = bytes.fromhex(
        "488d1d1f0000000fb63b85ff740cb8040000000f05"
        "48ffc3ebedb800000000bf2a0000000f05"
    ) + b"Eloquence is alive!\n\0"
    entry = 0x400000 + ELF64_HEADER_SIZE + ELF64_PROGRAM_HEADER_SIZE
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF" + bytes((2, 1, 1, 0)) + bytes(8),
        2, 62, 1, entry, ELF64_HEADER_SIZE, 0, 0, 64, 56, 1, 0, 0, 0,
    )
    program_header = struct.pack(
        "<IIQQQQQQ",
        1, 5, ELF64_HEADER_SIZE + ELF64_PROGRAM_HEADER_SIZE,
        entry, 0, len(code), len(code), 16,
    )
    return header + program_header + code


def make_spin_elf() -> bytes:
    code = bytes.fromhex("f390ebfc")
    entry = 0x400000 + ELF64_HEADER_SIZE + ELF64_PROGRAM_HEADER_SIZE
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF" + bytes((2, 1, 1, 0)) + bytes(8),
        2, 62, 1, entry, ELF64_HEADER_SIZE, 0, 0, 64, 56, 1, 0, 0, 0,
    )
    program_header = struct.pack(
        "<IIQQQQQQ",
        1, 5, ELF64_HEADER_SIZE + ELF64_PROGRAM_HEADER_SIZE,
        entry, 0, len(code), len(code), 16,
    )
    return header + program_header + code


def make_fat32_volume(
    total_sectors: int, volume_start: int, shell_size: int, demo_size: int
) -> tuple[bytes, bytes, bytes, int]:
    fat_sectors = math.ceil(
        (total_sectors - FAT_RESERVED_SECTORS + 2)
        / (SECTOR_SIZE // 4 + FAT_COUNT)
    )
    cluster_count = total_sectors - FAT_RESERVED_SECTORS - FAT_COUNT * fat_sectors
    if cluster_count < 65525:
        raise ValueError("partition is too small for a FAT32 test volume")
    shell_cluster_count = math.ceil(shell_size / SECTOR_SIZE)
    if shell_cluster_count == 0 or shell_cluster_count + 5 >= cluster_count:
        raise ValueError("invalid shell image size for FAT32 test volume")
    demo_cluster_count = math.ceil(demo_size / SECTOR_SIZE)
    if shell_cluster_count + demo_cluster_count + 5 >= cluster_count:
        raise ValueError("invalid demo image size for FAT32 test volume")

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xeb\x58\x90"
    boot[3:11] = b"ELOQUENC"
    put_u16(boot, 11, SECTOR_SIZE)
    boot[13] = 1
    put_u16(boot, 14, FAT_RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    put_u16(boot, 17, 0)
    put_u16(boot, 19, 0)
    boot[21] = 0xF8
    put_u16(boot, 22, 0)
    put_u16(boot, 24, 63)
    put_u16(boot, 26, 255)
    put_u32(boot, 28, volume_start)
    put_u32(boot, 32, total_sectors)
    put_u32(boot, 36, fat_sectors)
    put_u16(boot, 40, 0)
    put_u16(boot, 42, 0)
    put_u32(boot, 44, 2)
    put_u16(boot, 48, 1)
    put_u16(boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    put_u32(boot, 67, 0x454C4F51)
    boot[71:82] = b"ELOQUENCE  "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"

    fs_info = bytearray(SECTOR_SIZE)
    put_u32(fs_info, 0, 0x41615252)
    put_u32(fs_info, 484, 0x61417272)
    put_u32(fs_info, 488, cluster_count - 5 - shell_cluster_count)
    put_u32(fs_info, 492, 7 + shell_cluster_count)
    put_u32(fs_info, 508, 0xAA550000)

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    put_u32(fat, 0, 0x0FFFFFF8)
    put_u32(fat, 4, 0xFFFFFFFF)
    put_u32(fat, 8, 0x0FFFFFFF)
    put_u32(fat, 12, 0x0FFFFFFF)
    put_u32(fat, 16, 0x0FFFFFFF)
    for index in range(shell_cluster_count):
        cluster = 5 + index
        next_cluster = cluster + 1 if index + 1 < shell_cluster_count else 0x0FFFFFFF
        put_u32(fat, cluster * 4, next_cluster)
    demo_cluster_base = 5 + shell_cluster_count
    for index in range(demo_cluster_count):
        cluster = demo_cluster_base + index
        next_cluster = cluster + 1 if index + 1 < demo_cluster_count else 0x0FFFFFFF
        put_u32(fat, cluster * 4, next_cluster)
    put_u32(fat, (demo_cluster_base + demo_cluster_count) * 4, 0x0FFFFFFF)
    put_u32(fat, (demo_cluster_base + demo_cluster_count + 1) * 4, 0x0FFFFFFF)
    return bytes(boot), bytes(fs_info), bytes(fat), len(HELLO_TEXT), demo_cluster_base


def create_image(path: Path, size_mib: int, force: bool, shell_elf: bytes,
                 demo_elf: bytes) -> None:
    if size_mib < 64:
        raise ValueError("the FAT32 fixture needs at least 64 MiB")
    if path.exists() and not force:
        raise FileExistsError(f"{path} exists; pass --force to replace it")
    if not shell_elf:
        raise ValueError("the shell ELF must not be empty")
    if not demo_elf:
        raise ValueError("the demo ELF must not be empty")

    total_sectors = size_mib * 1024 * 1024 // SECTOR_SIZE
    backup_header_lba = total_sectors - 1
    backup_entries_lba = backup_header_lba - GPT_ARRAY_SECTORS
    first_usable_lba = 2 + GPT_ARRAY_SECTORS
    last_usable_lba = backup_entries_lba - 1
    partition_last_lba = last_usable_lba
    partition_sectors = partition_last_lba - PARTITION_START + 1

    entries = bytearray(GPT_ENTRY_COUNT * GPT_ENTRY_SIZE)
    entry = memoryview(entries)[:GPT_ENTRY_SIZE]
    entry[:16] = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7").bytes_le
    entry[16:32] = uuid.UUID("8ebd8668-65cf-42c8-9d37-d2f2b3eae8b1").bytes_le
    put_u64(entry, 32, PARTITION_START)
    put_u64(entry, 40, partition_last_lba)
    entry[56:128] = "Eloquence FAT32".encode("utf-16le").ljust(72, b"\0")
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid = uuid.UUID("8c9b3328-b07c-4e55-9d48-a5ec5c03b570")

    primary_header = make_gpt_header(
        1, backup_header_lba, first_usable_lba, last_usable_lba,
        2, disk_guid, entries_crc,
    )
    backup_header = make_gpt_header(
        backup_header_lba, 1, first_usable_lba, last_usable_lba,
        backup_entries_lba, disk_guid, entries_crc,
    )
    boot, fs_info, fat, contents_size, demo_cluster_base = make_fat32_volume(
        partition_sectors, PARTITION_START, len(shell_elf), len(demo_elf)
    )
    hello_elf = make_hello_elf()
    spin_elf = make_spin_elf()
    spin_cluster = 5 + math.ceil(len(shell_elf) / SECTOR_SIZE)
    script_cluster = spin_cluster + 1

    mode = "wb" if force else "xb"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open(mode) as image:
        image.truncate(size_mib * 1024 * 1024)

        mbr = bytearray(SECTOR_SIZE)
        mbr[446 + 1:446 + 4] = b"\xfe\xff\xff"
        mbr[446 + 4] = 0xEE
        mbr[446 + 5:446 + 8] = b"\xff\xff\xff"
        put_u32(mbr, 446 + 8, 1)
        put_u32(mbr, 446 + 12, min(total_sectors - 1, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        image.seek(0)
        image.write(mbr)
        image.seek(SECTOR_SIZE)
        image.write(primary_header)
        image.seek(2 * SECTOR_SIZE)
        image.write(entries)
        image.seek(backup_entries_lba * SECTOR_SIZE)
        image.write(entries)
        image.seek(backup_header_lba * SECTOR_SIZE)
        image.write(backup_header)

        volume_offset = PARTITION_START * SECTOR_SIZE
        image.seek(volume_offset)
        image.write(boot)
        image.seek(volume_offset + SECTOR_SIZE)
        image.write(fs_info)
        image.seek(volume_offset + 6 * SECTOR_SIZE)
        image.write(boot)
        image.seek(volume_offset + 7 * SECTOR_SIZE)
        image.write(fs_info)

        first_fat_sector = PARTITION_START + FAT_RESERVED_SECTORS
        image.seek(first_fat_sector * SECTOR_SIZE)
        image.write(fat)
        image.seek((first_fat_sector + len(fat) // SECTOR_SIZE) * SECTOR_SIZE)
        image.write(fat)

        data_start = first_fat_sector + FAT_COUNT * (len(fat) // SECTOR_SIZE)
        image.seek(data_start * SECTOR_SIZE)
        root = bytearray(SECTOR_SIZE)
        root[:11] = b"HELLO   TXT"
        root[11] = 0x20
        put_u16(root, 20, 0)
        put_u16(root, 26, 3)
        put_u32(root, 28, contents_size)
        root[32:43] = b"HELLO   ELF"
        root[43] = 0x20
        put_u16(root, 52, 0)
        put_u16(root, 58, 4)
        put_u32(root, 60, len(hello_elf))
        root[64:75] = b"SHELL   ELF"
        root[75] = 0x20
        put_u16(root, 84, 0)
        put_u16(root, 90, 5)
        put_u32(root, 92, len(shell_elf))
        root[96:107] = b"SPIN    ELF"
        root[107] = 0x20
        put_u16(root, 116, 0)
        put_u16(root, 122, spin_cluster)
        put_u32(root, 124, len(spin_elf))
        root[128:139] = b"CONTROL TXT"
        root[139] = 0x20
        put_u16(root, 148, 0)
        put_u16(root, 154, script_cluster)
        put_u32(root, 156, len(CONTROL_SCRIPT))
        root[160:171] = b"IPCDMO  ELF"
        root[171] = 0x20
        put_u16(root, 180, 0)
        put_u16(root, 186, demo_cluster_base)
        put_u32(root, 188, len(demo_elf))
        image.write(root)
        image.seek((data_start + 1) * SECTOR_SIZE)
        image.write(HELLO_TEXT)
        image.seek((data_start + 2) * SECTOR_SIZE)
        image.write(hello_elf)
        for index, offset in enumerate(range(0, len(shell_elf), SECTOR_SIZE)):
            cluster = 5 + index
            image.seek((data_start + cluster - 2) * SECTOR_SIZE)
            image.write(shell_elf[offset : offset + SECTOR_SIZE])
        image.seek((data_start + spin_cluster - 2) * SECTOR_SIZE)
        image.write(spin_elf)
        image.seek((data_start + script_cluster - 2) * SECTOR_SIZE)
        image.write(CONTROL_SCRIPT)
        for index, offset in enumerate(range(0, len(demo_elf), SECTOR_SIZE)):
            cluster = demo_cluster_base + index
            image.seek((data_start + cluster - 2) * SECTOR_SIZE)
            image.write(demo_elf[offset : offset + SECTOR_SIZE])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--size-mib", type=int, default=64)
    parser.add_argument("--shell", type=Path, required=True)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    create_image(args.image, args.size_mib, args.force, args.shell.read_bytes(),
                 args.demo.read_bytes())
    print(f"Created {args.image} ({args.size_mib} MiB, GPT + FAT32)")


if __name__ == "__main__":
    main()
