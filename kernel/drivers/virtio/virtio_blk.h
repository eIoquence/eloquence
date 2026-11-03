#ifndef ELOQUENCE_DRIVERS_VIRTIO_BLK_H
#define ELOQUENCE_DRIVERS_VIRTIO_BLK_H

#include <stdbool.h>

#include "arch/x86_64/pci.h"

/* Initialize the QEMU transitional PCI block device using its legacy I/O BAR. */
bool virtio_blk_initialize(const struct pci_device_info *device);

#endif
