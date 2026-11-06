#include <stdint.h>

#include "domain.h"

static uint64_t current_domain_id = KERNEL_DOMAIN_ID;

/* capability bits (bit numbers in uint32 masks) */
#define KERNEL_CAP_SYSTEM_CONTROL 0x01U
#define KERNEL_CAP_ADMIN 0x02U
#define KERNEL_CAP_MMIO 0x04U
#define KERNEL_CAP_FS_WRITE 0x08U
#define KERNEL_CAP_IPC 0x10U
#define KERNEL_CAP_ALL 0xFFFFFFFFU

#define KERNEL_DOMAIN_TABLE_SIZE 64U
static uint32_t domain_caps[KERNEL_DOMAIN_TABLE_SIZE];
static bool domain_caps_seen[KERNEL_DOMAIN_TABLE_SIZE];

static uint32_t domain_default_caps(uint64_t domain_id) {
    return domain_id == KERNEL_DOMAIN_ID ? KERNEL_CAP_ALL : KERNEL_CAP_ALL;
}

static uint32_t domain_index(uint64_t domain_id) {
    return (uint32_t)(domain_id & (uint64_t)(KERNEL_DOMAIN_TABLE_SIZE - 1U));
}

uint32_t kernel_domain_caps(uint64_t domain_id) {
    uint32_t index = domain_index(domain_id);
    if (!domain_caps_seen[index]) {
        domain_caps[index] = domain_default_caps(domain_id);
        domain_caps_seen[index] = true;
    }
    return domain_caps[index];
}

bool kernel_domain_grant(uint64_t domain_id, uint32_t bits) {
    uint32_t index = domain_index(domain_id);
    if (!domain_caps_seen[index]) {
        domain_caps[index] = domain_default_caps(domain_id);
        domain_caps_seen[index] = true;
    }
    domain_caps[index] |= bits;
    return true;
}

bool kernel_domain_revoke(uint64_t domain_id, uint32_t bits) {
    uint32_t index = domain_index(domain_id);
    if (!domain_caps_seen[index]) {
        domain_caps[index] = domain_default_caps(domain_id);
        domain_caps_seen[index] = true;
    }
    domain_caps[index] &= ~bits;
    return true;
}

bool kernel_domain_has(uint64_t domain_id, uint32_t bits) {
    return (kernel_domain_caps(domain_id) & bits) == bits;
}

uint64_t kernel_domain_current_id(void) {
    return current_domain_id;
}

void kernel_domain_enter(uint64_t domain_id) {
    current_domain_id = domain_id;
    /* load (or lazily default) the capabilities of the entered domain */
    (void)kernel_domain_caps(domain_id);
}

void kernel_domain_leave(void) {
    current_domain_id = KERNEL_DOMAIN_ID;
}
