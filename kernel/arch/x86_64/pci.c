#include <stdbool.h>
#include <stdint.h>

#include "io.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA 0x0cfc
#define PCI_CONFIG_ENABLE 0x80000000U

#define PCI_VENDOR_DEVICE 0x00
#define PCI_CLASS_REVISION 0x08
#define PCI_HEADER_TYPE 0x0c
#define PCI_BAR0 0x10

static uint32_t pci_read_config(uint8_t bus, uint8_t device,
                                uint8_t function, uint8_t offset) {
    uint32_t address = PCI_CONFIG_ENABLE
        | ((uint32_t)bus << 16)
        | ((uint32_t)device << 11)
        | ((uint32_t)function << 8)
        | (offset & 0xfc);

    x86_outl(PCI_CONFIG_ADDRESS, address);
    return x86_inl(PCI_CONFIG_DATA);
}

static uint8_t pci_bar_slot_count(uint8_t header_type) {
    switch (header_type & 0x7f) {
        case 0: return 6;
        case 1: return 2;
        case 2: return 1;
        default: return 0;
    }
}

static void pci_read_bars(struct pci_device_info *info) {
    uint8_t slot_count = pci_bar_slot_count(info->header_type);

    for (uint8_t index = 0; index < slot_count; index++) {
        uint32_t raw_low = pci_read_config(info->bus, info->device,
                                           info->function,
                                           PCI_BAR0 + index * 4);
        struct pci_bar_info *bar = &info->bars[info->bar_count++];

        bar->raw_low = raw_low;
        bar->index = index;
        bar->valid = true;

        if ((raw_low & 1) != 0) {
            bar->kind = PCI_BAR_IO;
            bar->base_address = raw_low & ~0x3U;
            continue;
        }

        uint8_t memory_type = (raw_low >> 1) & 0x3;
        bar->kind = memory_type == 3 ? PCI_BAR_RESERVED : PCI_BAR_MEMORY;
        bar->prefetchable = (raw_low & 0x8) != 0;
        bar->is_64_bit = memory_type == 2;
        bar->base_address = raw_low & ~0xfU;
        bar->valid = memory_type != 3;

        if (bar->is_64_bit) {
            if (index + 1 >= slot_count) {
                bar->valid = false;
                continue;
            }

            bar->raw_high = pci_read_config(info->bus, info->device,
                                            info->function,
                                            PCI_BAR0 + (index + 1) * 4);
            bar->base_address |= (uint64_t)bar->raw_high << 32;
            index++;
        }
    }
}

static bool pci_device_present(uint32_t vendor_device) {
    return (vendor_device & 0xffff) != 0xffff;
}

static void pci_read_device(struct pci_device_info *info, uint8_t bus,
                            uint8_t device, uint8_t function,
                            uint32_t vendor_device) {
    uint32_t class_revision = pci_read_config(bus, device, function,
                                              PCI_CLASS_REVISION);
    uint32_t header = pci_read_config(bus, device, function, PCI_HEADER_TYPE);

    info->bus = bus;
    info->device = device;
    info->function = function;
    info->vendor_id = (uint16_t)vendor_device;
    info->device_id = (uint16_t)(vendor_device >> 16);
    info->revision = (uint8_t)class_revision;
    info->programming_interface = (uint8_t)(class_revision >> 8);
    info->subclass = (uint8_t)(class_revision >> 16);
    info->class_code = (uint8_t)(class_revision >> 24);
    info->header_type = (uint8_t)(header >> 16);
    info->bar_count = 0;

    pci_read_bars(info);
}

static void pci_report_function(uint8_t bus, uint8_t device,
                                uint8_t function, uint32_t vendor_device,
                                pci_device_callback callback, void *context) {
    struct pci_device_info info = {0};

    pci_read_device(&info, bus, device, function, vendor_device);
    callback(&info, context);
}

uint64_t pci_enumerate(pci_device_callback callback, void *context) {
    uint64_t found = 0;

    if (callback == 0) {
        return 0;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t function_zero_id = pci_read_config((uint8_t)bus,
                                                       device, 0,
                                                       PCI_VENDOR_DEVICE);
            if (!pci_device_present(function_zero_id)) {
                continue;
            }

            uint8_t header_type = (uint8_t)(pci_read_config((uint8_t)bus,
                                                            device, 0,
                                                            PCI_HEADER_TYPE)
                                            >> 16);
            uint8_t function_count = (header_type & 0x80) != 0 ? 8 : 1;

            for (uint8_t function = 0; function < function_count; function++) {
                uint32_t vendor_device = function == 0
                    ? function_zero_id
                    : pci_read_config((uint8_t)bus, device, function,
                                      PCI_VENDOR_DEVICE);
                if (!pci_device_present(vendor_device)) {
                    continue;
                }

                pci_report_function((uint8_t)bus, device, function,
                                    vendor_device, callback, context);
                found++;
            }
        }
    }

    return found;
}
