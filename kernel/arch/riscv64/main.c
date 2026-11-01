#include <stdint.h>

#include <limine.h>

#include "arch/irq.h"
#include "console.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .mode = LIMINE_PAGING_MODE_RISCV_SV39,
    .max_mode = LIMINE_PAGING_MODE_RISCV_SV39,
    .min_mode = LIMINE_PAGING_MODE_RISCV_SV39
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_riscv_bsp_hartid_request hart_id_request = {
    .id = LIMINE_RISCV_BSP_HARTID_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_dtb_request dtb_request = {
    .id = LIMINE_DTB_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((noreturn)) void kmain(void) {
    struct limine_paging_mode_response *paging_response;
    struct limine_riscv_bsp_hartid_response *hart_response;
    struct limine_memmap_response *memory_map_response;

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_cpu_halt();
    }

    paging_response = paging_mode_request.response;
    if (paging_response == 0
        || paging_response->mode != LIMINE_PAGING_MODE_RISCV_SV39) {
        arch_cpu_halt();
    }

    riscv64_console_write("ELOQUENCE RV64\n");
    riscv64_console_write("PAGING MODE SV39\n");

    hart_response = hart_id_request.response;
    if (hart_response != 0) {
        riscv64_console_write("BOOT HART ");
        riscv64_console_write_u64(hart_response->bsp_hartid);
        riscv64_console_write("\n");
    } else {
        riscv64_console_write("BOOT HART UNKNOWN\n");
    }

    memory_map_response = memory_map_request.response;
    if (memory_map_response == 0
        || (memory_map_response->entry_count != 0
            && memory_map_response->entries == 0)) {
        riscv64_console_write("MEMMAP INVALID\n");
    } else {
        riscv64_console_write("MEMMAP ENTRIES ");
        riscv64_console_write_u64(memory_map_response->entry_count);
        riscv64_console_write("\n");
    }

    riscv64_console_write(dtb_request.response != 0
                          && dtb_request.response->dtb_ptr != 0
                          ? "DTB PRESENT\n" : "DTB MISSING\n");

    arch_cpu_halt();
}
