#ifndef ELOQUENCE_STORAGE_GPT_H
#define ELOQUENCE_STORAGE_GPT_H

#include <stdint.h>

#define GPT_MAX_PARTITIONS 128U

struct gpt_partition {
    uint32_t entry_number;
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
};

enum gpt_scan_result {
    GPT_SCAN_OK,
    GPT_SCAN_NOT_PRESENT,
    GPT_SCAN_IO_ERROR,
    GPT_SCAN_INVALID,
    GPT_SCAN_LIMIT_EXCEEDED
};

/*
 * Validates and reports the primary GPT only; this function never writes.
 * Calls must be serialized because the parser uses a shared scratch buffer.
 */
enum gpt_scan_result gpt_scan_device(uint64_t device_index,
                                     struct gpt_partition *partitions,
                                     uint32_t partition_capacity,
                                     uint32_t *partition_count);

#endif
