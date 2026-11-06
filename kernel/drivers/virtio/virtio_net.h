#ifndef ELOQUENCE_DRIVERS_VIRTIO_NET_H
#define ELOQUENCE_DRIVERS_VIRTIO_NET_H

#include <stdbool.h>

#include "arch/x86_64/pci.h"

/* Initialize the QEMU transitional PCI network device using its legacy I/O
 * BAR. Registers a transmit callback with the network interface and polls
 * for received frames through netif_poll(). */
bool virtio_net_initialize(const struct pci_device_info *device);

/* Poll the virtio-net device for newly received frames and deliver them to
 * the network interface. */
void virtio_net_poll(void);

#endif
