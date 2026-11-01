#include <stdbool.h>
#include <stdint.h>

#include "arch/mmio.h"
#include "arch/x86_64/mmio.h"
#include "edu.h"

#define EDU_PCI_VENDOR_ID 0x1234
#define EDU_PCI_DEVICE_ID 0x11e8
#define EDU_BAR0_MINIMUM_SIZE 8ULL
#define EDU_REG_IDENTITY 0x00ULL
#define EDU_REG_LIVENESS 0x04ULL
#define EDU_EXPECTED_IDENTITY 0x010000edU

bool edu_device_initialize(const struct pci_device_info *pci,
                           struct edu_device *device) {
    if (pci == 0 || device == 0
        || pci->vendor_id != EDU_PCI_VENDOR_ID
        || pci->device_id != EDU_PCI_DEVICE_ID
        || pci->bar_count == 0 || !pci->bars[0].valid
        || pci->bars[0].index != 0
        || pci->bars[0].kind != PCI_BAR_MEMORY
        || pci->bars[0].base_address == 0) {
        return false;
    }

    *device = (struct edu_device){0};
    device->bar0 = pci->bars[0];
    if (!pci_probe_bar_size(pci, &device->bar0, &device->bar0_size)
        || device->bar0_size < EDU_BAR0_MINIMUM_SIZE
        || !x86_mmio_map_range(device->bar0.base_address,
                               EDU_BAR0_MINIMUM_SIZE,
                               &device->registers)) {
        *device = (struct edu_device){0};
        return false;
    }
    device->initialized = true;

    uint32_t identity;
    if (!edu_device_read_identity(device, &identity)
        || identity != EDU_EXPECTED_IDENTITY) {
        (void)edu_device_shutdown(device);
        return false;
    }
    return true;
}

bool edu_device_read_identity(struct edu_device *device, uint32_t *identity) {
    return device != 0 && device->initialized && identity != 0
        && arch_mmio_read32(&device->registers, EDU_REG_IDENTITY, identity);
}

bool edu_device_liveness_roundtrip(struct edu_device *device,
                                   uint32_t value, uint32_t *before,
                                   uint32_t *after) {
    uint32_t original;
    uint32_t observed = 0;
    uint32_t restored = 0;

    if (device == 0 || !device->initialized || before == 0 || after == 0
        || !arch_mmio_read32(&device->registers, EDU_REG_LIVENESS, &original)) {
        return false;
    }

    bool write_ok = arch_mmio_write32(&device->registers, EDU_REG_LIVENESS,
                                      value);
    bool read_ok = write_ok
        && arch_mmio_read32(&device->registers, EDU_REG_LIVENESS, &observed);
    bool restore_ok = arch_mmio_write32(&device->registers, EDU_REG_LIVENESS,
                                        ~original);
    bool verify_ok = restore_ok
        && arch_mmio_read32(&device->registers, EDU_REG_LIVENESS, &restored);

    *before = original;
    *after = observed;
    return write_ok && read_ok && restore_ok && verify_ok
        && observed == ~value && restored == original;
}

bool edu_device_shutdown(struct edu_device *device) {
    if (device == 0 || !device->initialized) {
        return false;
    }
    if (!x86_mmio_unmap(&device->registers)) {
        return false;
    }
    *device = (struct edu_device){0};
    return true;
}
