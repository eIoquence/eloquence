#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/domain.h"
#include "paging.h"
#include "syscall_abi.h"
#include "user_mode.h"

#define IA32_EFER 0xc0000080U
#define IA32_STAR 0xc0000081U
#define IA32_LSTAR 0xc0000082U
#define IA32_FMASK 0xc0000084U
#define IA32_GS_BASE 0xc0000101U
#define IA32_KERNEL_GS_BASE 0xc0000102U

#define EFER_SYSCALL_ENABLE (1ULL << 0)
#define X86_RFLAGS_TRAP (1ULL << 8)
#define X86_RFLAGS_INTERRUPT (1ULL << 9)
#define X86_RFLAGS_DIRECTION (1ULL << 10)
#define X86_RFLAGS_NESTED_TASK (1ULL << 14)
#define X86_RFLAGS_ALIGNMENT_CHECK (1ULL << 18)

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_DATA_SELECTOR 0x1b
#define USER_CODE_SELECTOR 0x23
#define TSS_SELECTOR 0x28

struct x86_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

struct x86_gdt_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct x86_syscall_state {
    uint64_t user_stack_pointer;
    uint64_t kernel_stack_pointer;
};

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct x86_tss tss;
static uint8_t syscall_stack[16384] __attribute__((aligned(16)));
static struct x86_syscall_state syscall_state;
static bool user_mode_initialized;
static bool user_exit_occurred;
static bool user_fault_occurred;
static uint64_t user_exit_status;

uint64_t x86_user_kernel_cr3;
uint64_t x86_user_return_stack;

extern void x86_syscall_entry(void);
extern void x86_user_enter_asm(uint64_t entry, uint64_t stack_pointer,
                               uint64_t argument0, uint64_t argument1,
                               uint64_t argument2);

static bool syscall_supported(void) {
    uint32_t max_leaf = 0x80000000U;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__ volatile ("cpuid" : "+a"(max_leaf), "=b"(ebx), "=c"(ecx),
                      "=d"(edx));
    if (max_leaf < 0x80000001U) {
        return false;
    }

    eax = 0x80000001U;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx),
                      "=d"(edx));
    return (edx & (1U << 11)) != 0;
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

static void set_tss_descriptor(void) {
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;

    gdt[5] = (limit & 0xffff)
        | ((base & 0xffffff) << 16)
        | (0x89ULL << 40)
        | ((limit & 0xf0000) << 32)
        | ((base & 0xff000000) << 32);
    gdt[6] = base >> 32;
}

static bool install_gdt(void) {
    struct x86_gdt_register gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt
    };

    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00cf92000000ffffULL;
    gdt[3] = 0x00cff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;
    set_tss_descriptor();

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax"
        : : "m"(gdtr) : "rax", "memory");
    return true;
}

bool x86_user_mode_initialize(void) {
    __asm__ volatile ("cli" : : : "memory");
    if (user_mode_initialized || !syscall_supported()) {
        return false;
    }

    tss.rsp0 = (uint64_t)(uintptr_t)(syscall_stack + sizeof(syscall_stack));
    tss.io_map_base = sizeof(tss);
    syscall_state.kernel_stack_pointer = tss.rsp0;
    if (!install_gdt()) {
        return false;
    }

    uint64_t efer = read_msr(IA32_EFER);
    write_msr(IA32_EFER, efer | EFER_SYSCALL_ENABLE);
    write_msr(IA32_STAR, (0x10ULL << 48) | (0x08ULL << 32));
    write_msr(IA32_LSTAR, (uint64_t)(uintptr_t)x86_syscall_entry);
    write_msr(IA32_FMASK, X86_RFLAGS_TRAP | X86_RFLAGS_INTERRUPT
              | X86_RFLAGS_DIRECTION | X86_RFLAGS_NESTED_TASK
              | X86_RFLAGS_ALIGNMENT_CHECK);
    write_msr(IA32_GS_BASE, 0);
    write_msr(IA32_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&syscall_state);

    user_mode_initialized = true;
    return true;
}

bool x86_user_mode_run(const struct x86_address_space *address_space,
                       uint64_t entry, uint64_t stack_pointer,
                       uint64_t domain_id, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2,
                       uint64_t *exit_status, bool *faulted) {
    struct x86_address_space kernel_space;

    if (!user_mode_initialized || address_space == 0
        || address_space->root_virtual_address == 0
        || domain_id == KERNEL_DOMAIN_ID || exit_status == 0 || faulted == 0
        || (entry >> 47) != 0 || (stack_pointer >> 47) != 0
        || !x86_address_space_current(&kernel_space)) {
        return false;
    }

    x86_user_kernel_cr3 = kernel_space.root_physical_address;
    user_exit_occurred = false;
    user_fault_occurred = false;
    user_exit_status = 0;
    kernel_domain_enter(domain_id);
    x86_address_space_activate(address_space);
    x86_user_enter_asm(entry, stack_pointer, argument0, argument1, argument2);
    kernel_domain_leave();
    *exit_status = user_exit_status;
    *faulted = user_fault_occurred;
    return user_exit_occurred;
}

void x86_user_mode_set_exit_status(uint64_t status) {
    user_exit_status = status;
    user_exit_occurred = true;
}

uint64_t x86_user_mode_fault_exit(uint64_t vector) {
    user_exit_status = vector;
    user_fault_occurred = true;
    user_exit_occurred = true;
    kernel_domain_leave();
    return ELOQUENCE_SYSCALL_EXIT_SENTINEL;
}
