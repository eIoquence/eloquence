# VirtIO Block

The x86-64 kernel has a polling driver for the transitional VirtIO PCI block
device (`1af4:1001`) in legacy I/O mode. It discovers capacity, negotiates the
read-only and flush feature bits, configures the device-provided queue, and
registers a 512-byte-sector block device. Reads and writes use bounded DMA
buffers; block writes are available to kernel callers and flushing is exposed
when the device offers it. The serial monitor currently uses the read path.

The driver uses one serialized request at a time and polls the used ring with a
finite timeout. It supports a device queue up to 256 entries and transfers up to
128 KiB per block API request. DMA buffers must translate to contiguous physical
memory. The bootstrap paging code provides that translation for the kernel's
static buffers. The driver does not configure an IOMMU, handle device
interrupts, or support modern VirtIO PCI capabilities; only the QEMU
transitional interface is covered.

`make run` attaches `build/eloquence-userland.raw` as a legacy VirtIO block device.
When the file is absent, `tools/create-qemu-disk.py` creates a 64 MiB disposable
GPT disk with a FAT32 fixture containing `HELLO.TXT` and `HELLO.ELF`. Existing
disk files are left in place. Set `QEMU_DISK` to use another image.

See the [QEMU storage and filesystem run](../experiments/2026-11-03-qemu-storage-fs.md).
