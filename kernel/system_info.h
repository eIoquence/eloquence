#ifndef ELOQUENCE_SYSTEM_INFO_H
#define ELOQUENCE_SYSTEM_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

bool kernel_system_report(uint64_t report);
bool kernel_system_file_list(uint64_t device_index, uint64_t partition_number);
bool kernel_system_file_cat(uint64_t packed_name, uint64_t packed_extension);
uint32_t kernel_system_file_read_chunk(uint64_t packed_name,
                                      uint64_t packed_extension,
                                      uint64_t offset, uint32_t *data);
bool kernel_system_file_stat(uint64_t packed_name, uint64_t packed_extension);
bool kernel_system_file_create(uint64_t packed_name,
                               uint64_t packed_extension);
bool kernel_system_file_delete(uint64_t packed_name,
                               uint64_t packed_extension);
bool kernel_ipc_create(uint64_t channel);
bool kernel_ipc_send(uint64_t channel, uint64_t byte);
bool kernel_ipc_receive(uint64_t channel, uint8_t *byte);
bool kernel_system_ramfs_control(uint64_t operation, uint64_t argument0,
                                 uint64_t argument1);

bool kernel_system_file_copy_begin(uint64_t packed_name,
                                  uint64_t packed_extension,
                                  uint64_t mode);
bool kernel_system_file_copy_commit(uint64_t packed_name,
                                   uint64_t packed_extension);
bool kernel_system_file_write_begin(uint64_t packed_name,
                                    uint64_t packed_extension);
bool kernel_system_file_write_data(uint64_t packed_data, uint64_t bytes);
bool kernel_system_file_write_commit(void);
void kernel_system_file_write_abort(void);
bool kernel_system_directory_create(uint64_t packed_name,
                                    uint64_t packed_extension);
bool kernel_system_directory_change(uint64_t packed_name,
                                    uint64_t packed_extension);
bool kernel_system_file_list_current(void);
bool kernel_system_process_list(void);
bool kernel_system_process_exec(uint64_t packed_name,
                               uint64_t packed_extension);
uint64_t kernel_system_process_spawn(uint64_t packed_name,
                                    uint64_t packed_extension);

#endif
