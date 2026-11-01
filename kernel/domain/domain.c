#include <stdint.h>

#include "domain.h"

static uint64_t current_domain_id = KERNEL_DOMAIN_ID;

uint64_t kernel_domain_current_id(void) {
    return current_domain_id;
}

void kernel_domain_enter(uint64_t domain_id) {
    current_domain_id = domain_id;
}

void kernel_domain_leave(void) {
    current_domain_id = KERNEL_DOMAIN_ID;
}
