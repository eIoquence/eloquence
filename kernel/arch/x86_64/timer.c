#include <stdint.h>

#include "mmio.h"
#include "io.h"
#include "serial.h"
#include "timer.h"

#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xa0
#define PIC_SLAVE_DATA 0xa1
#define IMCR_INDEX 0x22
#define IMCR_DATA 0x23
#define IMCR_REGISTER 0x70
#define IMCR_APIC_MODE (1U << 0)
#define PIT_CHANNEL_0 0x40
#define PIT_COMMAND 0x43
#define PIT_INPUT_FREQUENCY_HZ 1193182U
#define IA32_APIC_BASE 0x1bU
#define IA32_APIC_BASE_ENABLE (1ULL << 11)
#define IA32_APIC_BASE_X2APIC (1ULL << 10)
#define APIC_BASE_MASK 0xfffff000ULL
#define APIC_TASK_PRIORITY 0x080U
#define APIC_EOI 0x0b0U
#define APIC_LVT_LINT0 0x350U
#define APIC_SPURIOUS_VECTOR 0x0f0U
#define APIC_SPURIOUS_VECTOR_VALUE 0xffU
#define APIC_LVT_DELIVERY_EXTINT (7U << 8)
#define APIC_LVT_MASKED (1U << 16)

static volatile uint64_t ticks;
static struct mmio_mapping local_apic_mapping;
static volatile uint32_t *local_apic;
static bool local_apic_enabled;

static void io_wait(void) {
    x86_outb(0x80, 0);
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high)
                      : "memory");
}

static bool cpu_has_local_apic(void) {
    uint32_t leaf = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__ volatile ("cpuid" : "+a"(leaf), "=b"(ebx), "=c"(ecx),
                      "=d"(edx));
    return (edx & (1U << 9)) != 0;
}

static void route_pic_to_local_apic(void) {
    x86_outb(IMCR_INDEX, IMCR_REGISTER);
    uint8_t imcr = x86_inb(IMCR_DATA);
    if (imcr == 0xffU) {
        serial_write("IMCR unavailable; retaining firmware PIC routing\n");
        return;
    }

    x86_outb(IMCR_INDEX, IMCR_REGISTER);
    x86_outb(IMCR_DATA, imcr | IMCR_APIC_MODE);
    x86_outb(IMCR_INDEX, IMCR_REGISTER);
    if ((x86_inb(IMCR_DATA) & IMCR_APIC_MODE) == 0) {
        serial_write("IMCR APIC routing did not latch; retaining current routing\n");
    }
}

static bool initialize_local_apic(void) {
    if (!cpu_has_local_apic()) {
        serial_write("CPU does not report local APIC support\n");
        return false;
    }

    uint64_t apic_base = read_msr(IA32_APIC_BASE);
    uint64_t physical_base = apic_base & APIC_BASE_MASK;

    if ((apic_base & IA32_APIC_BASE_X2APIC) != 0 || physical_base == 0) {
        serial_write("Local APIC mode unsupported; IA32_APIC_BASE=0x");
        serial_write_hex(apic_base);
        serial_write("\n");
        return false;
    }
    if ((apic_base & IA32_APIC_BASE_ENABLE) == 0) {
        write_msr(IA32_APIC_BASE, apic_base | IA32_APIC_BASE_ENABLE);
    }
    if (!x86_mmio_map_range(physical_base, 4096, &local_apic_mapping)) {
        serial_write("Local APIC MMIO mapping failed at 0x");
        serial_write_hex(physical_base);
        serial_write("\n");
        return false;
    }

    local_apic = (volatile uint32_t *)local_apic_mapping.address;
    local_apic[APIC_TASK_PRIORITY / sizeof(uint32_t)] = 0;
    uint32_t lint0 = local_apic[APIC_LVT_LINT0 / sizeof(uint32_t)];
    lint0 &= ~(7U << 8);
    lint0 |= APIC_LVT_DELIVERY_EXTINT;
    lint0 &= ~APIC_LVT_MASKED;
    local_apic[APIC_LVT_LINT0 / sizeof(uint32_t)] = lint0;
    local_apic[APIC_SPURIOUS_VECTOR / sizeof(uint32_t)] =
        APIC_SPURIOUS_VECTOR_VALUE | (1U << 8);
    route_pic_to_local_apic();
    local_apic_enabled = true;
    return true;
}

bool x86_timer_initialize(void) {
    uint16_t divisor = (uint16_t)(PIT_INPUT_FREQUENCY_HZ
                                  / X86_TIMER_FREQUENCY_HZ);

    x86_outb(PIC_MASTER_COMMAND, 0x11);
    io_wait();
    x86_outb(PIC_SLAVE_COMMAND, 0x11);
    io_wait();
    x86_outb(PIC_MASTER_DATA, 0x20);
    io_wait();
    x86_outb(PIC_SLAVE_DATA, 0x28);
    io_wait();
    x86_outb(PIC_MASTER_DATA, 0x04);
    io_wait();
    x86_outb(PIC_SLAVE_DATA, 0x02);
    io_wait();
    x86_outb(PIC_MASTER_DATA, 0x01);
    io_wait();
    x86_outb(PIC_SLAVE_DATA, 0x01);
    io_wait();

    /* Unmask only IRQ0; leave all slave and other master IRQs masked. */
    x86_outb(PIC_MASTER_DATA, 0xfe);
    x86_outb(PIC_SLAVE_DATA, 0xff);

    x86_outb(PIT_COMMAND, 0x36);
    x86_outb(PIT_CHANNEL_0, (uint8_t)divisor);
    x86_outb(PIT_CHANNEL_0, (uint8_t)(divisor >> 8));

    local_apic_enabled = initialize_local_apic();
    return local_apic_enabled;
}

uint64_t x86_timer_ticks(void) {
    return ticks;
}

bool x86_timer_uses_local_apic(void) {
    return local_apic_enabled;
}

void x86_timer_interrupt(void) {
    ticks++;
    if (local_apic_enabled) {
        local_apic[APIC_EOI / sizeof(uint32_t)] = 0;
    }
    x86_outb(PIC_MASTER_COMMAND, 0x20);
}
