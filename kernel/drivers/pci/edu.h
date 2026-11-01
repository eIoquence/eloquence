#ifndef ELOQUENCE_DRIVERS_PCI_EDU_H
#define ELOQUENCE_DRIVERS_PCI_EDU_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/mmio.h"
#include "pci.h"

struct edu_device {
    struct pci_bar_info bar0;
    struct mmio_mapping registers;
    uint64_t bar0_size;
    bool initialized;
};

/* If initialization fails after mapping, shut down the retained device state. */
bool edu_device_initialize(const struct pci_device_info *pci,
                           struct edu_device *device);
bool edu_device_read_identity(struct edu_device *device, uint32_t *identity);
bool edu_device_liveness_roundtrip(struct edu_device *device,
                                   uint32_t value, uint32_t *before,
                                   uint32_t *after);
bool edu_device_shutdown(struct edu_device *device);

#endif
