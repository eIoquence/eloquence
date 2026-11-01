# Legacy ATA PIO Driver

**Status:** x86-64 polled source prototype; not boot-validated

`kernel/drivers/ata/ata_pio.c` probes the legacy primary and secondary ATA
channels and registers detected LBA-capable disks with the bounded block-device
registry. Discovery issues `IDENTIFY DEVICE` only; it does not read or write
disk sectors. No driver is automatically selected for PCI AHCI controllers.

The first implementation supports 512-byte sectors and LBA28, capped at
2^28 sectors per device. Transfers use PIO with a finite status-poll limit,
interrupts disabled for the command sequence, and a maximum 128 KiB request.
The block API rejects out-of-range and overflowing requests before dispatch.
Writes happen only through an explicit `block_device_write` request and may
remain in volatile device cache. Call `block_device_flush` to request a device
flush; it returns failure when the command is unsupported or the device reports
an error. No crash-recovery semantics are implemented.

The implementation has no ATAPI, LBA48, AHCI, DMA, interrupt-driven operation,
partition parser, filesystem, media-change handling, or retry/recovery policy.
It is a minimal block-driver starting point, not a general storage stack. A
booted device test is still required before relying on reads or writes.
