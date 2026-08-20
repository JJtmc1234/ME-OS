# ME OS build. M1 target is a UEFI bootable ISO that Limine loads in QEMU.

ARCH        := x86_64
BUILD       := build
ISO_ROOT    := $(BUILD)/iso_root
KERNEL      := $(BUILD)/kernel.elf
ISO         := $(BUILD)/me-os.iso

LIMINE_DIR    := boot/limine
LIMINE_REPO   := https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH := v9.x-binary

OVMF_CODE  := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS  := /usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_LOCAL := $(BUILD)/OVMF_VARS.fd

CC := gcc
LD := ld

# GCC's own freestanding headers (stdint.h, stdbool.h, stddef.h). -nostdinc
# keeps the host's /usr/include out, so this path has to be added back.
FREESTANDING_INC := $(shell $(CC) -print-file-name=include)

# Freestanding: no libc, no runtime, no host assumptions. SSE and the x87
# unit are disabled because nothing has enabled them for us yet, and the red
# zone is unsafe once interrupts exist.
CFLAGS := -std=gnu11 -ffreestanding -nostdinc -O2 -g \
          -Wall -Wextra -Wshadow -Wconversion \
          -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -fno-PIE \
          -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
          -mno-red-zone -mcmodel=kernel \
          -isystem $(FREESTANDING_INC) \
          -Ikernel/include

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 --gc-sections -T linker.ld

SRCS := $(wildcard kernel/src/*.c)
OBJS := $(patsubst kernel/src/%.c,$(BUILD)/obj/%.o,$(SRCS))

.PHONY: all run test clean distclean

all: $(ISO)

$(LIMINE_DIR):
	git clone --depth 1 --branch $(LIMINE_BRANCH) $(LIMINE_REPO) $(LIMINE_DIR)

$(BUILD)/obj/%.o: kernel/src/%.c | $(LIMINE_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(OBJS) $(LDFLAGS) -o $@

$(ISO): $(KERNEL) limine.conf | $(LIMINE_DIR)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)

# A fresh copy, because OVMF writes its variable store back to this file.
$(OVMF_LOCAL): $(OVMF_VARS)
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $(OVMF_LOCAL)

QEMU_FLAGS := -machine q35 -m 512M -cdrom $(ISO) -boot d \
              -drive if=pflash,unit=0,format=raw,readonly=on,file=$(OVMF_CODE) \
              -drive if=pflash,unit=1,format=raw,file=$(OVMF_LOCAL) \
              -debugcon stdio -no-reboot -no-shutdown

run: $(ISO) $(OVMF_LOCAL)
	qemu-system-$(ARCH) $(QEMU_FLAGS)

# Headless boot that captures the screen, for checking M1 without a display.
test: $(ISO) $(OVMF_LOCAL)
	scripts/screenshot.sh

clean:
	$(RM) -r $(BUILD)

distclean: clean
	$(RM) -r $(LIMINE_DIR)
