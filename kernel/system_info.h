#ifndef ELOQUENCE_SYSTEM_INFO_H
#define ELOQUENCE_SYSTEM_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

bool kernel_system_report(uint64_t report);
bool kernel_system_file_list(uint64_t device_index, uint64_t partition_number);
bool kernel_system_file_cat(uint64_t packed_name, uint64_t packed_extension);
bool kernel_system_file_stat(uint64_t packed_name, uint64_t packed_extension);

#endif
