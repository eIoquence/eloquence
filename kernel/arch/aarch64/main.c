#include <stdint.h>

#include <limine.h>

#include "arch/irq.h"
#include "console.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
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

static uint64_t current_exception_level(void) {
    uint64_t current_el;

    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    return (current_el >> 2) & 3;
}

__attribute__((noreturn)) void kmain(void) {
    struct limine_framebuffer_response *framebuffer_response;
    struct limine_memmap_response *memory_map_response;

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_cpu_halt();
    }

    framebuffer_response = framebuffer_request.response;
    if (framebuffer_response == 0 || framebuffer_response->framebuffer_count == 0
        || framebuffer_response->framebuffers == 0
        || !aarch64_console_initialize(framebuffer_response->framebuffers[0])) {
        arch_cpu_halt();
    }

    aarch64_console_write("ELOQUENCE AARCH64\n");
    aarch64_console_write("BASE REVISION 6\n");
    aarch64_console_write("EXCEPTION LEVEL EL");
    aarch64_console_write_u64(current_exception_level());
    aarch64_console_write("\n");

    memory_map_response = memory_map_request.response;
    if (memory_map_response == 0
        || (memory_map_response->entry_count != 0
            && memory_map_response->entries == 0)) {
        aarch64_console_write("MEMMAP INVALID\n");
    } else {
        aarch64_console_write("MEMMAP ENTRIES ");
        aarch64_console_write_u64(memory_map_response->entry_count);
        aarch64_console_write("\n");
    }

    aarch64_console_write(dtb_request.response != 0
                          && dtb_request.response->dtb_ptr != 0
                          ? "DTB PRESENT\n" : "DTB MISSING\n");

    arch_cpu_halt();
}
