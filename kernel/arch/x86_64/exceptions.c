#include <stdint.h>

#include "domain/domain.h"
#include "exceptions.h"
#include "serial.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "timer.h"
#include "user_mode.h"

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_gate idt[256] __attribute__((aligned(16)));

extern void x86_irq0_stub(void);
extern void x86_spurious_stub(void);

#define DECLARE_EXCEPTION_STUB(vector) extern void x86_exception_stub_##vector(void)
DECLARE_EXCEPTION_STUB(0);
DECLARE_EXCEPTION_STUB(1);
DECLARE_EXCEPTION_STUB(2);
DECLARE_EXCEPTION_STUB(3);
DECLARE_EXCEPTION_STUB(4);
DECLARE_EXCEPTION_STUB(5);
DECLARE_EXCEPTION_STUB(6);
DECLARE_EXCEPTION_STUB(7);
DECLARE_EXCEPTION_STUB(8);
DECLARE_EXCEPTION_STUB(9);
DECLARE_EXCEPTION_STUB(10);
DECLARE_EXCEPTION_STUB(11);
DECLARE_EXCEPTION_STUB(12);
DECLARE_EXCEPTION_STUB(13);
DECLARE_EXCEPTION_STUB(14);
DECLARE_EXCEPTION_STUB(15);
DECLARE_EXCEPTION_STUB(16);
DECLARE_EXCEPTION_STUB(17);
DECLARE_EXCEPTION_STUB(18);
DECLARE_EXCEPTION_STUB(19);
DECLARE_EXCEPTION_STUB(20);
DECLARE_EXCEPTION_STUB(21);
DECLARE_EXCEPTION_STUB(22);
DECLARE_EXCEPTION_STUB(23);
DECLARE_EXCEPTION_STUB(24);
DECLARE_EXCEPTION_STUB(25);
DECLARE_EXCEPTION_STUB(26);
DECLARE_EXCEPTION_STUB(27);
DECLARE_EXCEPTION_STUB(28);
DECLARE_EXCEPTION_STUB(29);
DECLARE_EXCEPTION_STUB(30);
DECLARE_EXCEPTION_STUB(31);

static void (*const exception_stubs[32])(void) = {
    x86_exception_stub_0, x86_exception_stub_1,
    x86_exception_stub_2, x86_exception_stub_3,
    x86_exception_stub_4, x86_exception_stub_5,
    x86_exception_stub_6, x86_exception_stub_7,
    x86_exception_stub_8, x86_exception_stub_9,
    x86_exception_stub_10, x86_exception_stub_11,
    x86_exception_stub_12, x86_exception_stub_13,
    x86_exception_stub_14, x86_exception_stub_15,
    x86_exception_stub_16, x86_exception_stub_17,
    x86_exception_stub_18, x86_exception_stub_19,
    x86_exception_stub_20, x86_exception_stub_21,
    x86_exception_stub_22, x86_exception_stub_23,
    x86_exception_stub_24, x86_exception_stub_25,
    x86_exception_stub_26, x86_exception_stub_27,
    x86_exception_stub_28, x86_exception_stub_29,
    x86_exception_stub_30, x86_exception_stub_31
};

static const char *exception_name(uint64_t vector) {
    switch (vector) {
        case 0: return "divide error";
        case 1: return "debug";
        case 2: return "non-maskable interrupt";
        case 3: return "breakpoint";
        case 4: return "overflow";
        case 5: return "bound range exceeded";
        case 6: return "invalid opcode";
        case 7: return "device not available";
        case 8: return "double fault";
        case 9: return "coprocessor segment overrun";
        case 10: return "invalid TSS";
        case 11: return "segment not present";
        case 12: return "stack-segment fault";
        case 13: return "general protection fault";
        case 14: return "page fault";
        case 15: return "reserved exception";
        case 16: return "x87 floating-point";
        case 17: return "alignment check";
        case 18: return "machine check";
        case 19: return "SIMD floating-point";
        case 20: return "virtualization exception";
        case 21: return "control protection";
        case 28: return "hypervisor injection";
        case 29: return "VMM communication";
        case 30: return "security exception";
        default: return "reserved exception";
    }
}

void x86_exceptions_initialize(void) {
    uint16_t code_selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));

    for (uint16_t vector = 0; vector < 32; vector++) {
        uint64_t handler = (uint64_t)(uintptr_t)exception_stubs[vector];
        struct idt_gate *gate = &idt[vector];

        gate->offset_low = (uint16_t)handler;
        gate->selector = code_selector;
        gate->ist = 0;
        gate->attributes = 0x8e;
        gate->offset_middle = (uint16_t)(handler >> 16);
        gate->offset_high = (uint32_t)(handler >> 32);
        gate->reserved = 0;
    }

    uint64_t timer_handler = (uint64_t)(uintptr_t)x86_irq0_stub;
    struct idt_gate *timer_gate = &idt[32];
    timer_gate->offset_low = (uint16_t)timer_handler;
    timer_gate->selector = code_selector;
    timer_gate->ist = 0;
    timer_gate->attributes = 0x8e;
    timer_gate->offset_middle = (uint16_t)(timer_handler >> 16);
    timer_gate->offset_high = (uint32_t)(timer_handler >> 32);
    timer_gate->reserved = 0;

    uint64_t spurious_handler = (uint64_t)(uintptr_t)x86_spurious_stub;
    struct idt_gate *spurious_gate = &idt[0xff];
    spurious_gate->offset_low = (uint16_t)spurious_handler;
    spurious_gate->selector = code_selector;
    spurious_gate->ist = 0;
    spurious_gate->attributes = 0x8e;
    spurious_gate->offset_middle = (uint16_t)(spurious_handler >> 16);
    spurious_gate->offset_high = (uint32_t)(spurious_handler >> 32);
    spurious_gate->reserved = 0;

    struct idt_register idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}

uint64_t x86_exception_dispatch(const struct x86_exception_frame *frame) {
    if (frame == 0) {
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    uint64_t vector = frame->vector;
    serial_write("CPU exception: ");
    serial_write(exception_name(vector));
    serial_write(" vector=0x");
    serial_write_hex(vector);
    serial_write("\n");

    if (vector == 14) {
        uint64_t fault_address;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        serial_write("Page fault address: 0x");
        serial_write_hex(fault_address);
        serial_write("\n");
    }

    if ((frame->code_segment & 3) == 3
        && kernel_domain_current_id() != KERNEL_DOMAIN_ID) {
        serial_write("Terminating user domain after exception\n");
        if (kernel_scheduler_has_process(kernel_domain_current_id())) {
            (void)kernel_process_finish(kernel_domain_current_id(),
                                        x86_timer_ticks(), vector, true);
        }
        return x86_user_mode_fault_exit(vector);
    }

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
