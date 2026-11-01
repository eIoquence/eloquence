#ifndef ELOQUENCE_X86_64_PCI_H
#define ELOQUENCE_X86_64_PCI_H

#include <stdbool.h>
#include <stdint.h>

enum pci_bar_kind {
    PCI_BAR_MEMORY,
    PCI_BAR_IO,
    PCI_BAR_RESERVED
};

struct pci_bar_info {
    uint64_t base_address;
    uint32_t raw_low;
    uint32_t raw_high;
    uint8_t index;
    enum pci_bar_kind kind;
    bool is_64_bit;
    bool prefetchable;
    bool valid;
};

struct pci_device_info {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t revision;
    uint8_t programming_interface;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bar_count;
    struct pci_bar_info bars[6];
};

typedef void (*pci_device_callback)(const struct pci_device_info *device,
                                    void *context);

uint64_t pci_enumerate(pci_device_callback callback, void *context);
bool pci_probe_bar_size(const struct pci_device_info *device,
                        const struct pci_bar_info *bar, uint64_t *size);

#endif
