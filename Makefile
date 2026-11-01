SHELL := /bin/sh
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

TARGET_CC ?= clang
HOST_CC ?= cc
QEMU ?= qemu-system-x86_64
TARGET_ASFLAGS ?= --target=x86_64-unknown-none-elf -ffreestanding \
	-fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel

LIMINE_VERSION := v12.9.6
LIMINE_SHA256 := b38aabb042b0c0f9f562b7741be3c1204838d7bbacfa78bb411c674bd5b984ee
LIMINE_ARCHIVE := build/limine-binary.tar.gz
LIMINE_DIR := build/limine-binary
LIMINE := $(LIMINE_DIR)/limine

CLANG_RESOURCE_DIR := $(shell $(TARGET_CC) --print-resource-dir)
TARGET_CFLAGS ?= --target=x86_64-unknown-none-elf -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
	-fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -MMD -MP -m64 -march=x86-64 -mno-mmx -mno-sse \
	-mno-red-zone -mcmodel=kernel -nostdinc \
	-isystem $(CLANG_RESOURCE_DIR)/include -Iinclude -Ikernel -Ikernel/arch/x86_64
TARGET_LDFLAGS ?= --target=x86_64-unknown-none-elf -fuse-ld=lld -nostdlib -static \
	-Wl,--build-id=none -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 \
	-Wl,-z,noexecstack -Wl,-m,elf_x86_64 -Wl,-T,linker.ld

KERNEL_OBJECTS := build/main.o build/arch/x86_64/serial.o \
	build/arch/x86_64/pci.o build/arch/x86_64/paging.o \
	build/arch/x86_64/mmio.o \
	build/arch/x86_64/exceptions.o build/arch/x86_64/exceptions_stubs.o \
	build/capability/mmio_capability.o build/memory/page_allocator.o

-include $(KERNEL_OBJECTS:.o=.d)

.PHONY: all iso run clean

all: iso

iso: build/eloquence.iso

build:
	mkdir -p $@

$(LIMINE_ARCHIVE): | build
	curl --fail --location --silent --show-error \
		--output $@ \
		https://github.com/Limine-Bootloader/Limine/releases/download/$(LIMINE_VERSION)/limine-binary.tar.gz
	@if command -v sha256sum >/dev/null 2>&1; then \
		printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | sha256sum --check --status; \
	elif command -v shasum >/dev/null 2>&1; then \
		printf '%s  %s\n' '$(LIMINE_SHA256)' '$@' | shasum -a 256 -c -; \
	else \
		echo 'sha256sum or shasum is required to verify Limine' >&2; exit 1; \
	fi
	@echo 'Verified Limine $(LIMINE_VERSION) archive'

$(LIMINE_DIR)/limine.c: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $< -C build

$(LIMINE): $(LIMINE_DIR)/limine.c
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"

build/%.o: kernel/%.c
	mkdir -p $(@D)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

build/%.o: kernel/%.S
	mkdir -p $(@D)
	$(TARGET_CC) $(TARGET_ASFLAGS) -c $< -o $@

build/eloquence.elf: $(KERNEL_OBJECTS) linker.ld
	$(TARGET_CC) $(TARGET_LDFLAGS) -o $@ $(KERNEL_OBJECTS)

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

run: build/eloquence.iso
	$(QEMU) -machine q35 -m 512M -cdrom $< -boot d -display none \
		-device edu -serial stdio -monitor none -no-reboot -no-shutdown

clean:
	rm -rf build
