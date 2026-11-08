#ifndef ELOQUENCE_DOMAIN_H
#define ELOQUENCE_DOMAIN_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_DOMAIN_ID 0ULL

#define KERNEL_CAP_SYSTEM_CONTROL_BIT 0x01U
#define KERNEL_CAP_ADMIN_BIT 0x02U
#define KERNEL_CAP_MMIO_BIT 0x04U
#define KERNEL_CAP_FS_WRITE_BIT 0x08U
#define KERNEL_CAP_IPC_BIT 0x10U
#define KERNEL_CAP_PROCESS_BIT 0x20U

uint32_t kernel_domain_caps(uint64_t domain_id);
bool kernel_domain_grant(uint64_t domain_id, uint32_t bits);
bool kernel_domain_revoke(uint64_t domain_id, uint32_t bits);
bool kernel_domain_has(uint64_t domain_id, uint32_t bits);

uint64_t kernel_domain_current_id(void);
void kernel_domain_enter(uint64_t domain_id);
void kernel_domain_leave(void);

#endif
