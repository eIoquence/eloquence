SHELL := /bin/sh
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

TARGET_CC ?= clang
TARGET_CXX ?= clang++
TARGET_LD ?= ld.lld
HOST_CC ?= cc
QEMU ?= qemu-system-x86_64
QEMU_DISK ?= build/eloquence-shell.raw
PYTHON ?= python3
TARGET_ASFLAGS ?= --target=x86_64-unknown-none-elf -ffreestanding \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel

LIMINE_VERSION := v12.9.6
LIMINE_SHA256 := b38aabb042b0c0f9f562b7741be3c1204838d7bbacfa78bb411c674bd5b984ee
LIMINE_ARCHIVE := build/limine-binary.tar.gz
LIMINE_DIR := build/limine-binary
LIMINE_STAMP := $(LIMINE_DIR)/.unpacked
LIMINE := $(LIMINE_DIR)/limine

CLANG_RESOURCE_DIR := $(shell $(TARGET_CC) --print-resource-dir)
TARGET_CFLAGS ?= --target=x86_64-unknown-none-elf -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
	-fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -MMD -MP -m64 -march=x86-64 -mno-mmx -mno-sse \
	-mno-red-zone -mcmodel=kernel -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude -Ikernel -Ikernel/arch/x86_64
TARGET_LDFLAGS ?= --build-id=none --gc-sections -z max-page-size=0x1000 \
	-z noexecstack -m elf_x86_64 -T linker.ld

AARCH64_CFLAGS ?= --target=aarch64-unknown-none-elf -std=gnu11 -O2 \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-stack-check -fno-pic -fno-pie -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-MMD -MP -march=armv8-a -mgeneral-regs-only -mcmodel=large -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude -Ikernel \
	-Ikernel/arch/aarch64
AARCH64_ASFLAGS ?= --target=aarch64-unknown-none-elf -ffreestanding \
	-fno-pic -fno-pie -march=armv8-a -mgeneral-regs-only -mcmodel=large
AARCH64_LDFLAGS ?= --build-id=none --gc-sections -m aarch64elf \
	-z max-page-size=0x1000 -z noexecstack -T linker-aarch64.ld

RISCV64_CFLAGS ?= --target=riscv64-unknown-none-elf -std=gnu11 -O2 \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-stack-check -fno-pic -fno-pie -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-MMD -MP -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -mno-relax \
	-nostdinc -isystem $(CLANG_RESOURCE_DIR)/include -Iinclude -Ikernel \
	-Ikernel/arch/riscv64
RISCV64_ASFLAGS ?= --target=riscv64-unknown-none-elf -ffreestanding \
	-fno-pic -fno-pie -march=rv64imac_zicsr -mabi=lp64 \
	-mcmodel=medany -mno-relax
RISCV64_LDFLAGS ?= --build-id=none --gc-sections --no-relax -m elf64lriscv \
	-z max-page-size=0x1000 -T linker-riscv64.ld

MIPS32_CFLAGS ?= --target=mipsel-unknown-elf -std=gnu11 -O2 \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -mno-abicalls \
	-march=mips32r2 -mabi=32 -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude -Ikernel
USER_CFLAGS ?= --target=x86_64-unknown-none-elf -std=gnu11 -Os \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-stack-check -fno-pic -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -MMD -MP -m64 -march=x86-64 \
	-mno-mmx -mno-sse -mno-red-zone -mcmodel=small -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude
USER_CXXFLAGS ?= --target=x86_64-unknown-none-elf -std=gnu++11 -Os \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -MMD -MP -m64 -march=x86-64 \
	-mno-mmx -mno-sse -mno-red-zone -mcmodel=small -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude
USER_LDFLAGS ?= --build-id=none --gc-sections -z max-page-size=0x1000 \
	-z noexecstack -m elf_x86_64 -T userland/user.ld

KERNEL_OBJECTS := build/main.o build/arch/x86_64/irq.o \
	build/arch/x86_64/serial.o \
	build/arch/x86_64/pci.o build/arch/x86_64/paging.o \
	build/arch/x86_64/mmio.o build/arch/x86_64/user_mode.o \
	build/arch/x86_64/syscall.o build/arch/x86_64/syscall_entry.o \
	build/arch/x86_64/exceptions.o build/arch/x86_64/exceptions_stubs.o \
	build/arch/x86_64/timer.o build/arch/x86_64/timer_irq.o \
	build/capability/mmio_capability.o build/domain/domain.o \
	build/process/process.o build/process/scheduler.o \
	build/process/waitqueue.o \
	build/lib/memory.o build/memory/page_allocator.o \
	build/storage/block_device.o build/storage/gpt.o build/storage/fat32.o \
	build/storage/vfs.o build/storage/ramfs.o \
	build/monitor/serial_shell.o \
	build/drivers/ata/ata_pio.o build/drivers/pci/edu.o \
	build/drivers/virtio/virtio_blk.o build/drivers/virtio/virtio_net.o \
	build/drivers/video/fb.o \
	build/drivers/input/ps2.o build/drivers/input/input_queue.o \
	build/net/netif.o build/net/ethernet.o build/net/arp.o build/net/ipv4.o build/net/firewall.o build/net/socket.o build/net/udp.o build/net/tcp.o build/ipc/ipc.o \
	build/user/mmio_demo.o build/user/elf_loader.o
AARCH64_OBJECTS := build/aarch64/arch/aarch64/main.o \
	build/aarch64/arch/aarch64/console.o build/aarch64/arch/aarch64/irq.o
RISCV64_OBJECTS := build/riscv64/arch/riscv64/main.o \
	build/riscv64/arch/riscv64/console.o build/riscv64/arch/riscv64/irq.o
MIPS32_OBJECTS := build/mips32/arch/mips32/irq.o

-include $(KERNEL_OBJECTS:.o=.d)
-include $(AARCH64_OBJECTS:.o=.d)
-include $(RISCV64_OBJECTS:.o=.d)
-include $(MIPS32_OBJECTS:.o=.d)
-include build/userland/shell.d

.PHONY: all iso run qemu-disk userland-elf x86_64-objects aarch64-objects aarch64-elf riscv64-objects \
	riscv64-elf mips32-objects clean

all: iso

iso: build/eloquence.iso

x86_64-objects: $(KERNEL_OBJECTS)

build:
	mkdir -p $@

$(LIMINE_ARCHIVE): | build
	curl --fail --location --silent --show-error \
		--output $@ \
		https://github.com/Limine-Bootloader/Limine/releases/download/$(LIMINE_VERSION)/limine-binary.tar.gz
	@if printf '%s  %s\n' \
		'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' \
		'/dev/null' | sha256sum --check --status >/dev/null 2>&1; then \
		printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | sha256sum --check --status; \
	elif command -v shasum >/dev/null 2>&1; then \
		printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | shasum -a 256 -c -; \
	else \
		echo 'sha256sum or shasum is required to verify Limine' >&2; exit 1; \
	fi
	@echo 'Verified Limine $(LIMINE_VERSION) archive'

$(LIMINE_STAMP): $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $< -C build
	touch $@

$(LIMINE): $(LIMINE_STAMP)
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"

build/%.o: kernel/%.c
	mkdir -p $(@D)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

build/%.o: kernel/%.S
	mkdir -p $(@D)
	$(TARGET_CC) $(TARGET_ASFLAGS) -c $< -o $@

build/aarch64/%.o: kernel/%.c
	mkdir -p $(@D)
	$(TARGET_CC) $(AARCH64_CFLAGS) -c $< -o $@

build/aarch64/%.o: kernel/%.S
	mkdir -p $(@D)
	$(TARGET_CC) $(AARCH64_ASFLAGS) -c $< -o $@

aarch64-objects: $(AARCH64_OBJECTS)

build/aarch64/eloquence.elf: $(AARCH64_OBJECTS) linker-aarch64.ld
	$(TARGET_LD) $(AARCH64_LDFLAGS) -o $@ $(AARCH64_OBJECTS)

aarch64-elf: build/aarch64/eloquence.elf

build/riscv64/%.o: kernel/%.c
	mkdir -p $(@D)
	$(TARGET_CC) $(RISCV64_CFLAGS) -c $< -o $@

build/riscv64/%.o: kernel/%.S
	mkdir -p $(@D)
	$(TARGET_CC) $(RISCV64_ASFLAGS) -c $< -o $@

riscv64-objects: $(RISCV64_OBJECTS)

build/riscv64/eloquence.elf: $(RISCV64_OBJECTS) linker-riscv64.ld
	$(TARGET_LD) $(RISCV64_LDFLAGS) -o $@ $(RISCV64_OBJECTS)

riscv64-elf: build/riscv64/eloquence.elf

build/mips32/%.o: kernel/%.c
	mkdir -p $(@D)
	$(TARGET_CC) $(MIPS32_CFLAGS) -c $< -o $@

mips32-objects: $(MIPS32_OBJECTS)

build/userland/shell.o: userland/shell.c include/eloquence/syscall_abi.h
	mkdir -p $(@D)
	$(TARGET_CC) $(USER_CFLAGS) -c $< -o $@

build/userland/shell.elf: build/userland/shell.o userland/user.ld
	$(TARGET_LD) $(USER_LDFLAGS) -o $@ $<

build/userland/ipc_demo.o: userland/ipc_demo.c include/eloquence/syscall_abi.h
	mkdir -p $(@D)
	$(TARGET_CC) $(USER_CFLAGS) -c $< -o $@

build/userland/ipc_demo.elf: build/userland/ipc_demo.o userland/user.ld
	$(TARGET_LD) $(USER_LDFLAGS) -o $@ $<

build/userland/atmosphere/%.o: userland/atmosphere/%.cpp include/eloquence/syscall_abi.h
	mkdir -p $(@D)
	$(TARGET_CXX) $(USER_CXXFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -c $< -o $@

build/userland/atmosphere/compositor.elf: build/userland/atmosphere/compositor.o build/userland/atmosphere/gfx.o build/userland/atmosphere/window.o build/userland/atmosphere/font.o build/userland/atmosphere/ui.o userland/user.ld
	$(TARGET_LD) $(USER_LDFLAGS) -o $@ build/userland/atmosphere/compositor.o build/userland/atmosphere/gfx.o build/userland/atmosphere/window.o build/userland/atmosphere/font.o build/userland/atmosphere/ui.o

build/userland/simple_test.o: userland/simple_test.c include/eloquence/syscall_abi.h
	mkdir -p $(@D)
	$(TARGET_CC) $(USER_CFLAGS) -c $< -o $@

build/userland/simple_test.elf: build/userland/simple_test.o userland/user.ld
	$(TARGET_LD) $(USER_LDFLAGS) -o $@ $<

userland-elf: build/userland/shell.elf build/userland/ipc_demo.elf build/userland/atmosphere/compositor.elf build/userland/simple_test.elf

build/eloquence.elf: $(KERNEL_OBJECTS) linker.ld
	$(TARGET_LD) $(TARGET_LDFLAGS) -o $@ $(KERNEL_OBJECTS)

build/eloquence.iso: $(LIMINE) build/eloquence.elf limine.conf
	rm -rf build/iso-root
	mkdir -p build/iso-root/boot/limine build/iso-root/EFI/BOOT
	cp build/eloquence.elf build/iso-root/boot/eloquence.elf
	cp limine.conf $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
		$(LIMINE_DIR)/limine-uefi-cd.bin build/iso-root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI build/iso-root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso-root -o $@
	$(LIMINE) bios-install $@
	rm -rf build/iso-root

qemu-disk: build/userland/shell.elf build/userland/ipc_demo.elf build/userland/atmosphere/compositor.elf build/userland/simple_test.elf
	$(PYTHON) tools/create-qemu-disk.py "$(QEMU_DISK)" \
		--shell build/userland/shell.elf \
		--demo build/userland/ipc_demo.elf \
		--atmosphere build/userland/atmosphere/compositor.elf \
		--simple-test build/userland/simple_test.elf --force

run: build/eloquence.iso qemu-disk
	$(QEMU) -machine q35 -m 512M -cdrom $< -boot d -display none \
		-device edu \
		-drive if=none,file=$(QEMU_DISK),format=raw,id=vd0 \
		-device virtio-blk-pci,drive=vd0,disable-modern=on \
		-serial stdio -monitor none -no-reboot -no-shutdown

clean:
	rm -rf build
