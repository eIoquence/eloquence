#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/irq.h"
#include "arch/x86_64/io.h"
#include "ata_pio.h"
#include "storage/block_device.h"

#define ATA_CHANNEL_COUNT 2
#define ATA_DRIVES_PER_CHANNEL 2
#define ATA_DEVICE_CAPACITY (ATA_CHANNEL_COUNT * ATA_DRIVES_PER_CHANNEL)
#define ATA_SECTOR_SIZE 512U
#define ATA_POLL_LIMIT 1000000U
#define ATA_LBA28_SECTOR_LIMIT (1ULL << 28)

#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA_LOW 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HIGH 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7

#define ATA_STATUS_ERROR 0x01
#define ATA_STATUS_DEVICE_FAULT 0x20
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_BUSY 0x80
#define ATA_STATUS_ABSENT 0xff

#define ATA_CONTROL_DISABLE_INTERRUPTS 0x02
#define ATA_COMMAND_IDENTIFY 0xec
#define ATA_COMMAND_READ_SECTORS 0x20
#define ATA_COMMAND_WRITE_SECTORS 0x30
#define ATA_COMMAND_FLUSH_CACHE 0xe7

struct ata_pio_device {
    uint16_t command_base;
    uint16_t control_base;
    uint8_t drive_select;
    uint8_t channel_index;
    uint64_t sector_count;
};

struct ata_channel {
    uint16_t command_base;
    uint16_t control_base;
    bool locked;
};

static struct ata_channel channels[ATA_CHANNEL_COUNT] = {
    { .command_base = 0x1f0, .control_base = 0x3f6 },
    { .command_base = 0x170, .control_base = 0x376 }
};

static struct ata_pio_device ata_devices[ATA_DEVICE_CAPACITY];
static uint64_t ata_device_count;
static bool ata_initialized;

static uint16_t command_port(const struct ata_pio_device *device,
                             uint8_t register_offset) {
    return device->command_base + register_offset;
}

static void delay_400ns(const struct ata_pio_device *device) {
    (void)x86_inb(device->control_base);
    (void)x86_inb(device->control_base);
    (void)x86_inb(device->control_base);
    (void)x86_inb(device->control_base);
}

static bool wait_not_busy(const struct ata_pio_device *device,
                          uint8_t *status_out) {
    for (uint32_t attempt = 0; attempt < ATA_POLL_LIMIT; attempt++) {
        uint8_t status = x86_inb(command_port(device, ATA_REG_STATUS));
        if (status == ATA_STATUS_ABSENT) {
            return false;
        }
        if ((status & ATA_STATUS_BUSY) == 0) {
            if (status_out != 0) {
                *status_out = status;
            }
            return true;
        }
    }
    return false;
}

static bool wait_data_request(const struct ata_pio_device *device) {
    for (uint32_t attempt = 0; attempt < ATA_POLL_LIMIT; attempt++) {
        uint8_t status = x86_inb(command_port(device, ATA_REG_STATUS));
        if (status == ATA_STATUS_ABSENT
            || (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0) {
            return false;
        }
        if ((status & ATA_STATUS_BUSY) == 0
            && (status & ATA_STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

static bool select_device(struct ata_pio_device *device) {
    uint8_t status;

    x86_outb(device->control_base, ATA_CONTROL_DISABLE_INTERRUPTS);
    x86_outb(command_port(device, ATA_REG_DRIVE), device->drive_select);
    delay_400ns(device);

    uint8_t initial_status = x86_inb(command_port(device, ATA_REG_STATUS));
    return initial_status != 0 && initial_status != ATA_STATUS_ABSENT
        && wait_not_busy(device, &status);
}

static void decode_model(const uint16_t identify[256], char model[41]) {
    size_t output = 0;

    for (size_t word = 27; word <= 46; word++) {
        uint16_t value = identify[word];
        char pair[2] = { (char)(value >> 8), (char)value };
        for (size_t i = 0; i < 2; i++) {
            char character = pair[i];
            model[output++] = character >= 0x20 && character <= 0x7e
                ? character : ' ';
        }
    }
    while (output != 0 && model[output - 1] == ' ') {
        output--;
    }
    model[output] = '\0';
}

static bool identify_device(struct ata_pio_device *device,
                            uint16_t identify[256], uint64_t *sectors) {
    uint8_t status;

    if (!select_device(device)) {
        return false;
    }

    x86_outb(command_port(device, ATA_REG_SECTOR_COUNT), 0);
    x86_outb(command_port(device, ATA_REG_LBA_LOW), 0);
    x86_outb(command_port(device, ATA_REG_LBA_MID), 0);
    x86_outb(command_port(device, ATA_REG_LBA_HIGH), 0);
    x86_outb(command_port(device, ATA_REG_COMMAND), ATA_COMMAND_IDENTIFY);

    status = x86_inb(command_port(device, ATA_REG_STATUS));
    if (status == 0 || status == ATA_STATUS_ABSENT
        || !wait_not_busy(device, &status)
        || (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0) {
        return false;
    }

    /* ATAPI devices report a nonzero signature in these task-file registers. */
    if (x86_inb(command_port(device, ATA_REG_LBA_MID)) != 0
        || x86_inb(command_port(device, ATA_REG_LBA_HIGH)) != 0
        || !wait_data_request(device)) {
        return false;
    }

    x86_insw(command_port(device, ATA_REG_DATA), identify, 256);
    if ((identify[49] & (1U << 9)) == 0) {
        return false;
    }

    uint64_t capacity = (uint32_t)identify[60]
        | ((uint32_t)identify[61] << 16);
    if (capacity == 0) {
        return false;
    }

    *sectors = capacity < ATA_LBA28_SECTOR_LIMIT
        ? capacity : ATA_LBA28_SECTOR_LIMIT;
    return true;
}

static bool transfer_sectors(struct ata_pio_device *device, uint64_t lba,
                             uint32_t count, void *buffer, bool write) {
    uint32_t remaining = count;
    uint8_t *cursor = buffer;
    uint8_t status;

    if (device == 0 || buffer == 0
        || count == 0 || count > 256
        || lba >= device->sector_count
        || (uint64_t)count > device->sector_count - lba
        || !select_device(device)) {
        return false;
    }

    x86_outb(command_port(device, ATA_REG_DRIVE),
             (uint8_t)(0xe0 | (device->drive_select & 0x10)
                       | ((lba >> 24) & 0x0f)));
    delay_400ns(device);
    x86_outb(command_port(device, ATA_REG_SECTOR_COUNT),
             (uint8_t)count);
    x86_outb(command_port(device, ATA_REG_LBA_LOW), (uint8_t)lba);
    x86_outb(command_port(device, ATA_REG_LBA_MID), (uint8_t)(lba >> 8));
    x86_outb(command_port(device, ATA_REG_LBA_HIGH), (uint8_t)(lba >> 16));
    x86_outb(command_port(device, ATA_REG_COMMAND), write
             ? ATA_COMMAND_WRITE_SECTORS : ATA_COMMAND_READ_SECTORS);

    while (remaining != 0) {
        if (!wait_data_request(device)) {
            return false;
        }
        if (write) {
            x86_outsw(command_port(device, ATA_REG_DATA), cursor,
                      ATA_SECTOR_SIZE / sizeof(uint16_t));
        } else {
            x86_insw(command_port(device, ATA_REG_DATA), cursor,
                     ATA_SECTOR_SIZE / sizeof(uint16_t));
        }
        cursor += ATA_SECTOR_SIZE;
        remaining--;
    }

    if (!wait_not_busy(device, &status)
        || (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0) {
        return false;
    }
    return true;
}

static bool ata_block_read(void *context, uint64_t lba, uint32_t count,
                           void *buffer) {
    struct ata_pio_device *device = context;
    struct ata_channel *channel;
    bool success;
    arch_irq_state_t irq_flags;

    if (device == 0 || device->channel_index >= ATA_CHANNEL_COUNT) {
        return false;
    }
    irq_flags = arch_irq_save();
    channel = &channels[device->channel_index];
    if (__atomic_test_and_set(&channel->locked, __ATOMIC_ACQUIRE)) {
        arch_irq_restore(irq_flags);
        return false;
    }
    success = transfer_sectors(device, lba, count, buffer, false);
    __atomic_clear(&channel->locked, __ATOMIC_RELEASE);
    arch_irq_restore(irq_flags);
    return success;
}

static bool ata_block_write(void *context, uint64_t lba, uint32_t count,
                            const void *buffer) {
    struct ata_pio_device *device = context;
    struct ata_channel *channel;
    bool success;
    arch_irq_state_t irq_flags;

    if (device == 0 || device->channel_index >= ATA_CHANNEL_COUNT) {
        return false;
    }
    irq_flags = arch_irq_save();
    channel = &channels[device->channel_index];
    if (__atomic_test_and_set(&channel->locked, __ATOMIC_ACQUIRE)) {
        arch_irq_restore(irq_flags);
        return false;
    }
    success = transfer_sectors(device, lba, count, (void *)buffer, true);
    __atomic_clear(&channel->locked, __ATOMIC_RELEASE);
    arch_irq_restore(irq_flags);
    return success;
}

static bool ata_block_flush(void *context) {
    struct ata_pio_device *device = context;
    struct ata_channel *channel;
    arch_irq_state_t irq_flags;
    uint8_t status;

    if (device == 0 || device->channel_index >= ATA_CHANNEL_COUNT) {
        return false;
    }
    irq_flags = arch_irq_save();
    channel = &channels[device->channel_index];
    if (__atomic_test_and_set(&channel->locked, __ATOMIC_ACQUIRE)) {
        arch_irq_restore(irq_flags);
        return false;
    }

    bool success = select_device(device);
    if (success) {
        x86_outb(command_port(device, ATA_REG_COMMAND),
                 ATA_COMMAND_FLUSH_CACHE);
        success = wait_not_busy(device, &status)
            && (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) == 0;
    }

    __atomic_clear(&channel->locked, __ATOMIC_RELEASE);
    arch_irq_restore(irq_flags);
    return success;
}

static bool register_device(struct ata_pio_device *device, uint64_t index,
                            const char *model) {
    struct block_device block = {
        .name = { 'a', 't', 'a', '0' + (char)index, '\0' },
        .sector_size = ATA_SECTOR_SIZE,
        .sector_count = device->sector_count,
        .read_only = false,
        .context = device,
        .read = ata_block_read,
        .write = ata_block_write,
        .flush = ata_block_flush
    };

    for (size_t i = 0; i < BLOCK_DEVICE_MODEL_LENGTH - 1 && model[i] != '\0'; i++) {
        block.model[i] = model[i];
    }
    return block_device_register(&block);
}

bool ata_pio_initialize(void) {
    if (ata_initialized) {
        return false;
    }
    ata_initialized = true;

    for (uint8_t channel_index = 0;
         channel_index < ATA_CHANNEL_COUNT; channel_index++) {
        for (uint8_t drive_index = 0;
             drive_index < ATA_DRIVES_PER_CHANNEL; drive_index++) {
            struct ata_pio_device candidate = {
                .command_base = channels[channel_index].command_base,
                .control_base = channels[channel_index].control_base,
                .drive_select = (uint8_t)(0xa0 | (drive_index << 4)),
                .channel_index = channel_index
            };
            uint16_t identify[256];
            uint64_t sectors = 0;
            char model[BLOCK_DEVICE_MODEL_LENGTH] = {0};

            if (!identify_device(&candidate, identify, &sectors)) {
                continue;
            }
            decode_model(identify, model);
            candidate.sector_count = sectors;
            ata_devices[ata_device_count] = candidate;
            if (register_device(&ata_devices[ata_device_count],
                                ata_device_count, model)) {
                ata_device_count++;
            } else {
                ata_devices[ata_device_count] = (struct ata_pio_device){0};
            }
            if (ata_device_count == ATA_DEVICE_CAPACITY) {
                return true;
            }
        }
    }
    return true;
}
