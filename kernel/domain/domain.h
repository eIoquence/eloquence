#ifndef ELOQUENCE_DOMAIN_H
#define ELOQUENCE_DOMAIN_H

#include <stdint.h>

#define KERNEL_DOMAIN_ID 0ULL

uint64_t kernel_domain_current_id(void);
void kernel_domain_enter(uint64_t domain_id);
void kernel_domain_leave(void);

#endif
