#ifndef ELOQUENCE_SYSTEM_INFO_H
#define ELOQUENCE_SYSTEM_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

bool kernel_system_report(uint64_t report);

#endif
