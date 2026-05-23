SHELL := /bin/sh

ifeq ($(V),1)
Q :=
quiet =
MAKE_QUIET :=
else
Q := @
quiet = @printf '  %-7s %s\n' "$(1)" "$(2)"
MAKE_QUIET := -s
endif

PROJECT := xbks
ARCH := x86_64

BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
ISO_IMAGE := $(BUILD_DIR)/$(PROJECT).iso
KERNEL_ELF := $(BUILD_DIR)/$(PROJECT).elf
RAMDISK ?=
SYSTEM_FONT ?=

ifeq ($(V),1)
XORRISO_LOG :=
LIMINE_INSTALL_LOG :=
LIMINE_CONFIGURE_LOG :=
else
XORRISO_LOG := >$(BUILD_DIR)/xorriso.log 2>&1 || { cat $(BUILD_DIR)/xorriso.log; exit 1; }
LIMINE_INSTALL_LOG := >$(BUILD_DIR)/limine-install.log 2>&1 || { cat $(BUILD_DIR)/limine-install.log; exit 1; }
LIMINE_CONFIGURE_LOG := >$(abspath $(BUILD_DIR)/limine-configure.log) 2>&1 || { cat $(abspath $(BUILD_DIR)/limine-configure.log); exit 1; }
endif

LIMINE_DIR := third_party/limine
LIMINE_BIN_DIR := $(LIMINE_DIR)/bin
LIMINE := $(LIMINE_BIN_DIR)/limine
LIMINE_PROTOCOL_INCLUDE := $(LIMINE_DIR)/limine-protocol/include
LIMINE_READY := $(BUILD_DIR)/limine.ready
LIMINE_PRODUCTS := \
	$(LIMINE) \
	$(LIMINE_BIN_DIR)/limine-bios-cd.bin \
	$(LIMINE_BIN_DIR)/limine-bios.sys \
	$(LIMINE_BIN_DIR)/limine-uefi-cd.bin \
	$(LIMINE_BIN_DIR)/BOOTX64.EFI \
	$(LIMINE_PROTOCOL_INCLUDE)/limine.h

ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif
ifeq ($(origin OBJCOPY),default)
OBJCOPY := llvm-objcopy
endif
ifeq ($(origin OBJDUMP),default)
OBJDUMP := llvm-objdump
endif
ifeq ($(origin READELF),default)
READELF := llvm-readelf
endif
QEMU ?= qemu-system-x86_64
QEMU_DISPLAY ?=
XORRISO ?= xorriso

CFLAGS := \
	-target x86_64-unknown-none-elf \
	-std=c23 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-pic \
	-fno-pie \
	-fno-omit-frame-pointer \
	-fno-strict-aliasing \
	-m64 \
	-march=x86-64 \
	-mcmodel=kernel \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wconversion \
	-Wshadow \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Werror=implicit-function-declaration \
	-g3 \
	-O2 \
	-MMD \
	-MP \
	-Iinclude \
	-I$(LIMINE_PROTOCOL_INCLUDE) \
	-I$(LIMINE_DIR)/flanterm/src

THIRD_PARTY_CFLAGS := \
	-target x86_64-unknown-none-elf \
	-std=c23 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-pic \
	-fno-pie \
	-fno-omit-frame-pointer \
	-fno-strict-aliasing \
	-m64 \
	-march=x86-64 \
	-mcmodel=kernel \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-Wall \
	-Wextra \
	-Wno-error \
	-g3 \
	-O2 \
	-MMD \
	-MP \
	-Iinclude \
	-I$(LIMINE_PROTOCOL_INCLUDE) \
	-I$(LIMINE_DIR)/flanterm/src

LDFLAGS := \
	-nostdlib \
	-static \
	-m elf_x86_64 \
	-z max-page-size=0x1000 \
	-T boot/linker.ld

KERNEL_SOURCES := \
	$(wildcard boot/*.c) \
	$(wildcard kernel/*.c) \
	$(wildcard arch/$(ARCH)/*.c) \
	$(wildcard drivers/*.c) \
	$(wildcard console/*.c) \
	$(wildcard lib/*.c) \
	$(wildcard mm/*.c) \
	$(wildcard fs/*.c) \
	$(wildcard syscall/*.c) \
	$(LIMINE_DIR)/flanterm/src/flanterm.c \
	$(LIMINE_DIR)/flanterm/src/flanterm_backends/fb.c

KERNEL_ASM_SOURCES := \
	$(wildcard arch/$(ARCH)/*.S)

KERNEL_OBJECTS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_SOURCES)) \
	$(patsubst %.S,$(BUILD_DIR)/%.o,$(KERNEL_ASM_SOURCES))
KERNEL_DEPS := $(KERNEL_OBJECTS:.o=.d)

.PHONY: all iso run run-headless clean distclean limine limine-clean

all: $(KERNEL_ELF)

iso: $(ISO_IMAGE)

run: $(ISO_IMAGE)
	$(call quiet,QEMU,$(ISO_IMAGE))
	$(Q)$(QEMU) \
		-M q35 \
		-cdrom $(ISO_IMAGE) \
		-boot d \
		-m 256M \
		-serial stdio \
		$(QEMU_DISPLAY) \
		-debugcon file:$(BUILD_DIR)/debugcon.log \
		-no-reboot \
		-no-shutdown

run-headless:
	$(Q)$(MAKE) $(MAKE_QUIET) --no-print-directory run QEMU_DISPLAY="-display none" $(if $(V),V=$(V))

$(KERNEL_ELF): $(KERNEL_OBJECTS) boot/linker.ld
	$(call quiet,LD,$@)
	$(Q)$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(BUILD_DIR)/%.o: %.c $(LIMINE_READY)
	@mkdir -p $(dir $@)
	$(call quiet,CC,$<)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S $(LIMINE_READY)
	@mkdir -p $(dir $@)
	$(call quiet,AS,$<)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(LIMINE_DIR)/%.o: $(LIMINE_DIR)/%.c $(LIMINE_READY)
	@mkdir -p $(dir $@)
	$(call quiet,CC,$<)
	$(Q)$(CC) $(THIRD_PARTY_CFLAGS) -c $< -o $@

$(ISO_IMAGE): $(KERNEL_ELF) boot/limine.conf $(LIMINE_READY) $(if $(RAMDISK),$(RAMDISK)) $(if $(SYSTEM_FONT),$(SYSTEM_FONT))
	$(call quiet,ISO,$@)
	$(Q)rm -rf $(ISO_ROOT)
	$(Q)mkdir -p $(ISO_ROOT)/boot $(ISO_ROOT)/EFI/BOOT
	$(Q)cp $(KERNEL_ELF) $(ISO_ROOT)/boot/xbks.elf
	$(Q)cp boot/limine.conf $(ISO_ROOT)/boot/limine.conf
	$(Q)if [ -n "$(RAMDISK)" ]; then \
		cp "$(RAMDISK)" $(ISO_ROOT)/boot/ramdisk.tar; \
		printf '\n    module_path: boot():/boot/ramdisk.tar\n    module_string: ramdisk\n' >> $(ISO_ROOT)/boot/limine.conf; \
	fi
	$(Q)if [ -n "$(SYSTEM_FONT)" ]; then \
		cp "$(SYSTEM_FONT)" $(ISO_ROOT)/boot/system.ttf; \
		printf '\n    module_path: boot():/boot/system.ttf\n    module_string: system-font\n' >> $(ISO_ROOT)/boot/limine.conf; \
	fi
	$(Q)cp $(LIMINE_BIN_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine-bios.sys
	$(Q)cp $(LIMINE_BIN_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine-bios-cd.bin
	$(Q)cp $(LIMINE_BIN_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine-uefi-cd.bin
	$(Q)cp $(LIMINE_BIN_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	$(Q)$(XORRISO) -as mkisofs -quiet -R -r -J \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO_IMAGE) $(XORRISO_LOG)
	$(call quiet,LIMINE,bios-install $(ISO_IMAGE))
	$(Q)$(LIMINE) bios-install $(ISO_IMAGE) $(LIMINE_INSTALL_LOG)

$(LIMINE_READY):
	@mkdir -p $(BUILD_DIR)
	$(call quiet,LIMINE,check)
	$(Q)set -eu; \
	missing=0; \
	for product in $(LIMINE_PRODUCTS); do \
		if [ ! -e "$$product" ]; then missing=1; fi; \
	done; \
	if [ "$$missing" -eq 0 ]; then \
		printf '  %-7s %s\n' "LIMINE" "ready"; \
	else \
		printf '  %-7s %s\n' "LIMINE" "build"; \
		cd $(LIMINE_DIR); \
		if [ ! -x ./configure ]; then ./bootstrap; fi; \
		./configure \
			--enable-bios \
			--enable-bios-cd \
			--enable-uefi-x86-64 \
			--enable-uefi-cd \
			--disable-uefi-ia32 \
			--disable-uefi-aarch64 \
			--disable-uefi-riscv64 \
			--disable-uefi-loongarch64 \
			--disable-bios-pxe \
			CC=$(CC) \
			CC_FOR_TARGET=$(CC) \
			LD_FOR_TARGET=$(LD) \
			OBJCOPY_FOR_TARGET=$(OBJCOPY) \
			OBJDUMP_FOR_TARGET=$(OBJDUMP) \
			READELF_FOR_TARGET=$(READELF) $(LIMINE_CONFIGURE_LOG); \
		$(MAKE) $(MAKE_QUIET) --no-print-directory -C .; \
	fi
	@mkdir -p $(BUILD_DIR)
	@touch $@

limine: $(LIMINE_READY)

limine-clean:
	$(call quiet,CLEAN,limine)
	$(Q)$(MAKE) $(MAKE_QUIET) --no-print-directory -C $(LIMINE_DIR) clean || true
	$(Q)rm -f $(LIMINE_READY)

clean:
	$(call quiet,CLEAN,xbks/$(BUILD_DIR))
	$(Q)rm -rf $(BUILD_DIR)

distclean: clean limine-clean

-include $(KERNEL_DEPS)
