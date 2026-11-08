#include <stdint.h>

#include "domain.h"

/* Identity of the domain currently executing on this CPU. Single-CPU today;
 * future SMP work should move this to per-CPU storage (see F-05). */
static uint64_t current_domain_id = KERNEL_DOMAIN_ID;

/* capability bits (bit numbers in uint32 masks) */
#define KERNEL_CAP_SYSTEM_CONTROL 0x01U
#define KERNEL_CAP_ADMIN 0x02U
#define KERNEL_CAP_MMIO 0x04U
#define KERNEL_CAP_FS_WRITE 0x08U
#define KERNEL_CAP_IPC 0x10U
#define KERNEL_CAP_PROCESS 0x20U
#define KERNEL_CAP_ALL 0xFFFFFFFFU

/* Default capabilities: absolutely nothing except the kernel domain. Every
 * right must be granted explicitly; there are no implicit privileges. */
#define KERNEL_CAP_DEFAULT 0U
#define KERNEL_CAP_KERNEL_DEFAULT KERNEL_CAP_ALL

#define KERNEL_DOMAIN_TABLE_SIZE 64U

struct domain_entry {
    bool in_use;
    uint64_t domain_id;   /* canonical id, for collision detection */
    uint32_t caps;
};

static struct domain_entry domains[KERNEL_DOMAIN_TABLE_SIZE];

static uint32_t domain_index(uint64_t domain_id) {
    return (uint32_t)(domain_id & (uint64_t)(KERNEL_DOMAIN_TABLE_SIZE - 1U));
}

static uint32_t domain_default_caps(uint64_t domain_id) {
    return domain_id == KERNEL_DOMAIN_ID ? KERNEL_CAP_KERNEL_DEFAULT
                                         : KERNEL_CAP_DEFAULT;
}

uint32_t kernel_domain_caps(uint64_t domain_id) {
    uint32_t index = domain_index(domain_id);
    struct domain_entry *entry = &domains[index];
    if (!entry->in_use || entry->domain_id != domain_id) {
        entry->in_use = true;
        entry->domain_id = domain_id;
        entry->caps = domain_default_caps(domain_id);
    }
    return entry->caps;
}

bool kernel_domain_grant(uint64_t domain_id, uint32_t bits) {
    uint64_t current = kernel_domain_current_id();
    uint32_t index = domain_index(domain_id);
    struct domain_entry *entry = &domains[index];

    if (!entry->in_use || entry->domain_id != domain_id) {
        entry->in_use = true;
        entry->domain_id = domain_id;
        entry->caps = domain_default_caps(domain_id);
    }

    /* The kernel authority may grant any capability. Any other domain may only
     * grant capabilities it already possesses - no self-escalation. */
    if (current != KERNEL_DOMAIN_ID) {
        uint32_t current_caps = kernel_domain_caps(current);
        if ((current_caps & bits) != bits) {
            return false; /* grantor does not hold these rights */
        }
        /* Only the kernel may grant administrative rights to another domain. */
        if ((bits & KERNEL_CAP_ADMIN) != 0U && domain_id != current) {
            return false;
        }
    }

    entry->caps |= bits;
    return true;
}

bool kernel_domain_revoke(uint64_t domain_id, uint32_t bits) {
    uint64_t current = kernel_domain_current_id();
    uint32_t index = domain_index(domain_id);
    struct domain_entry *entry = &domains[index];

    if (!entry->in_use || entry->domain_id != domain_id) {
        return false; /* unknown domain */
    }

    /* Only the kernel may revoke administrative rights. */
    if (current != KERNEL_DOMAIN_ID && (bits & KERNEL_CAP_ADMIN) != 0U) {
        return false;
    }

    entry->caps &= ~bits;
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
