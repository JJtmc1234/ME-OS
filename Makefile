# ME OS build.
#
# M1 target is a UEFI bootable ISO that Limine loads in QEMU.
#
#   make            build build/me-os.iso
#   make run        boot it in QEMU with a window
#   make test       boot it headless and check what was drawn
#   make test-unit  run the host side framebuffer checks, no emulator needed
#   make check      tools, then build, then test
#   make clean      remove build output
#
# Nothing here writes to a disk, a USB device, or any host device. The only
# outputs are files under build/.

ARCH        := x86_64
BUILD       := build
ISO_ROOT    := $(BUILD)/iso_root
KERNEL      := $(BUILD)/kernel.elf
ISO         := $(BUILD)/me-os.iso

LIMINE_DIR  := boot/limine
LIMINE_REPO := https://github.com/limine-bootloader/limine.git
# A tag, not a branch. A moving branch would make the build unreproducible.
LIMINE_TAG  := v9.6.7-binary
LIMINE_TOOL := $(LIMINE_DIR)/limine

CC      := gcc
LD      := ld
XORRISO := xorriso
QEMU    := qemu-system-$(ARCH)
GIT     := git

# Timestamps baked into the ISO. Overridable, fixed by default, so two builds
# of the same source produce the same image. The HFS+ and Apple partition map
# options mkisofs offers are left off deliberately: they only help Macs boot
# the disc, and they write a fresh identifier into every image, which would
# make the ISO different on every build for no benefit here.
SOURCE_DATE_EPOCH ?= 1735689600
ISO_DATE          ?= 2025010100000000
# Without this xorriso invents a random GPT identifier per build.
ISO_GUID          ?= 4d452d4f-5300-4d31-9000-000000000001
# Four bytes, spelling MEOS, written where Limine's installer would otherwise
# put a random number.
ISO_MBR_ID        ?= \115\105\117\123

# OVMF is the UEFI firmware QEMU boots. Distributions disagree about where it
# lives, so take the first candidate that exists rather than hardcoding one.
OVMF_CODE_CANDIDATES := \
	/usr/share/OVMF/OVMF_CODE_4M.fd \
	/usr/share/OVMF/OVMF_CODE.fd \
	/usr/share/edk2/ovmf/OVMF_CODE.fd \
	/usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
	/usr/share/qemu/edk2-x86_64-code.fd
OVMF_VARS_CANDIDATES := \
	/usr/share/OVMF/OVMF_VARS_4M.fd \
	/usr/share/OVMF/OVMF_VARS.fd \
	/usr/share/edk2/ovmf/OVMF_VARS.fd \
	/usr/share/edk2-ovmf/x64/OVMF_VARS.fd \
	/usr/share/qemu/edk2-i386-vars.fd

OVMF_CODE  ?= $(firstword $(wildcard $(OVMF_CODE_CANDIDATES)))
OVMF_VARS  ?= $(firstword $(wildcard $(OVMF_VARS_CANDIDATES)))
OVMF_LOCAL := $(BUILD)/OVMF_VARS.fd

# GCC's own freestanding headers (stdint.h, stdbool.h, stddef.h). -nostdinc
# keeps the host's /usr/include out, so this path has to be added back.
FREESTANDING_INC := $(shell $(CC) -print-file-name=include)

# Freestanding: no libc, no runtime, no host assumptions. SSE and the x87
# unit are disabled because nothing has enabled them for us yet, and the red
# zone is unsafe once interrupts exist.
# -ffile-prefix-map keeps the build directory out of the binary, so the same
# source builds byte for byte identically from any path.
CFLAGS := -std=gnu11 -ffreestanding -nostdinc -O2 -g \
          -Wall -Wextra -Wshadow -Wconversion \
          -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -fno-PIE \
          -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
          -mno-red-zone -mcmodel=kernel \
          -ffile-prefix-map=$(CURDIR)=. \
          -isystem $(FREESTANDING_INC) \
          -Ikernel/include \
          -MMD -MP

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 --gc-sections \
           --build-id=none -T linker.ld

# sort, so link order does not depend on how the filesystem lists the directory.
SRCS := $(sort $(wildcard kernel/src/*.c))
OBJS := $(patsubst kernel/src/%.c,$(BUILD)/obj/%.o,$(SRCS))

# Editing a header has to rebuild what includes it. Without this the build
# succeeded, linked objects compiled against the old header, and reran test
# binaries that were never recompiled, all reporting a clean pass. Two objects
# disagreeing about a struct layout link without complaint, and the wrong
# behaviour then shows up at runtime in a file nobody edited.
#
# Kernel objects use the compiler's own dependency files. -MMD writes one .d
# beside each .o listing the headers that object really included, and -MP adds
# an empty rule for each header so deleting one does not wedge the build. The
# minus on -include is there for the first build, when no .d exists yet.
DEPS := $(OBJS:.o=.d)

# Host tests compile several sources in one command, so per object .d files do
# not fit. They depend on every header instead. That rebuilds more than strictly
# needed, which for ten small binaries costs a second and is always right.
HEADERS := $(sort $(wildcard kernel/include/*.h))

.PHONY: all iso check check-tools run run-iso run-bios run-serial run-vbox \
        vbox-capture vbox-remove test test-unit clean distclean help

all: $(ISO)

# A name that says what it makes, since `make` on its own does not.
iso: $(ISO)

help:
	@echo "make            build $(ISO)"
	@echo "make iso        the same thing, said plainly"
	@echo "make run        boot the ISO in QEMU with a window"
	@echo "                it saves its filesystem to $(DISK),"
	@echo "                which is kept between runs and removed by make clean"
	@echo "make run-iso    the same, and never anything but the ISO"
	@echo "make run-bios   boot the same ISO through BIOS instead of UEFI"
	@echo "make run-vbox   boot the same ISO in VirtualBox"
	@echo "make test       boot headless and verify the screen automatically"
	@echo "make test-unit  host side framebuffer bounds checks, no emulator"
	@echo "make check      check tools, build, then test"
	@echo "make check-tools verify the toolchain is installed"
	@echo "make clean      remove $(BUILD)/"
	@echo "make distclean  also remove the fetched $(LIMINE_DIR)/"

# One place that says what the build needs and how to get it.
check-tools:
	@fail=0; \
	for tool in $(CC) $(LD) $(XORRISO) $(GIT); do \
		command -v $$tool >/dev/null 2>&1 || { \
			echo "missing build tool: $$tool" >&2; fail=1; }; \
	done; \
	command -v $(QEMU) >/dev/null 2>&1 || { \
		echo "missing emulator: $(QEMU)  (needed by make run and make test)" >&2; fail=1; }; \
	command -v python3 >/dev/null 2>&1 || { \
		echo "missing python3  (needed by make test)" >&2; fail=1; }; \
	if [ -z "$(OVMF_CODE)" ] || [ -z "$(OVMF_VARS)" ]; then \
		echo "missing OVMF UEFI firmware. Looked in:" >&2; \
		for p in $(OVMF_CODE_CANDIDATES); do echo "    $$p" >&2; done; \
		echo "  set OVMF_CODE= and OVMF_VARS= if it lives elsewhere" >&2; fail=1; \
	fi; \
	if [ $$fail -ne 0 ]; then \
		echo "" >&2; \
		echo "On Debian or Ubuntu:" >&2; \
		echo "  sudo apt install build-essential xorriso qemu-system-x86 ovmf git python3" >&2; \
		exit 1; \
	fi; \
	echo "toolchain ok"; \
	echo "  compiler: $$($(CC) --version | head -1)"; \
	echo "  emulator: $$($(QEMU) --version | head -1)"; \
	echo "  firmware: $(OVMF_CODE)"

$(LIMINE_DIR):
	@command -v $(GIT) >/dev/null 2>&1 || { \
		echo "git is needed to fetch Limine ($(LIMINE_TAG))" >&2; exit 1; }
	$(GIT) clone --depth 1 --branch $(LIMINE_TAG) $(LIMINE_REPO) $(LIMINE_DIR)

# Floating point is enabled for one file and no others.
#
# The kernel is built with SSE off, so the compiler cannot emit a floating
# point instruction anywhere by accident, including in code that runs before
# the processor has been told to allow them. geometry.c is the exception,
# because it is the only file that does arithmetic, and everything it exposes
# takes and returns integers so nothing else needs SSE to call it.
GEOMETRY_CFLAGS := $(filter-out -mno-sse -mno-sse2,$(CFLAGS))

$(BUILD)/obj/geometry.o: kernel/src/geometry.c
	@mkdir -p $(dir $@)
	$(CC) $(GEOMETRY_CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: kernel/src/%.c
	@mkdir -p $(dir $@)
	@command -v $(CC) >/dev/null 2>&1 || { \
		echo "missing compiler: $(CC). Run make check-tools." >&2; exit 1; }
	$(CC) $(CFLAGS) -c $< -o $@

# Floating point must stay where it was put. If the compiler starts emitting
# SSE anywhere else, it would run before the processor has been told to allow
# it, and the machine would fault on a instruction nobody wrote deliberately.
.PHONY: check-fp-isolation
check-fp-isolation: $(OBJS)
	@for object in $(filter-out $(BUILD)/obj/geometry.o,$(OBJS)); do \
		if objdump -d $$object | grep -qE '%xmm|movsd|mulsd|addsd'; then \
			echo "$$object contains SSE instructions; only geometry.o may" >&2; \
			exit 1; \
		fi; \
	done; \
	objdump -d $(BUILD)/obj/geometry.o | grep -qE 'mulsd|addsd' \
		|| { echo "geometry.o has no floating point in it at all" >&2; exit 1; }
	@echo "floating point is confined to geometry.o"

$(KERNEL): $(OBJS) linker.ld check-fp-isolation
	@mkdir -p $(dir $@)
	$(LD) $(OBJS) $(LDFLAGS) -o $@

# Limine's own installer, which stamps the BIOS boot record into the finished
# image. Built from the source in the release drop rather than downloaded
# separately, so the tool and the boot files are always the same version.
$(LIMINE_TOOL): | $(LIMINE_DIR)
	$(MAKE) -C $(LIMINE_DIR)

# One ISO that boots two ways.
#
# It was UEFI only, which is what QEMU with OVMF wants and is not what most
# other machines offer first. VirtualBox boots BIOS by default and its EFI is a
# separate switch, and a real machine may be set either way. A hybrid image
# carries both: an El Torito BIOS boot catalogue and an EFI system partition,
# and the firmware picks whichever it understands.
#
# Two images, one per firmware, was the alternative. One image is better for the
# same reason one log is: two of them is two answers to what ME OS is, and the
# one that gets tested is not necessarily the one that gets booted.
$(ISO): $(KERNEL) limine.conf $(LIMINE_TOOL) | $(LIMINE_DIR)
	@command -v $(XORRISO) >/dev/null 2>&1 || { \
		echo "missing $(XORRISO), needed to build the ISO. Run make check-tools." >&2; exit 1; }
	@test -f $(LIMINE_DIR)/BOOTX64.EFI || { \
		echo "$(LIMINE_DIR) is present but incomplete. Run make distclean, then make." >&2; exit 1; }
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	$(XORRISO) -as mkisofs -R -r -J \
		-volid ME_OS --modification-date=$(ISO_DATE) \
		--set_all_file_dates $(ISO_DATE) \
		--gpt_disk_guid $(ISO_GUID) \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)
	$(LIMINE_TOOL) bios-install $(ISO)
	@# The installer writes a random four byte MBR disk identifier at 0x1B8,
	@# which is the one thing in the image that differs between two builds of
	@# the same source. It identifies the disk to a partition table nothing here
	@# uses, so a fixed value is as correct as a random one and is checkable.
	@# The same reasoning as ISO_DATE and ISO_GUID above.
	printf '$(ISO_MBR_ID)' | dd of=$(ISO) bs=1 seek=440 conv=notrunc status=none

# A fresh copy, because OVMF writes its variable store back to this file.
$(OVMF_LOCAL):
	@test -n "$(OVMF_VARS)" || { \
		echo "no OVMF variable store found. Run make check-tools." >&2; exit 1; }
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $(OVMF_LOCAL)
	chmod u+w $(OVMF_LOCAL)

# The disk ME OS saves its filesystem to, kept between runs on purpose. That is
# the whole point of it: what you do in ME OS is still there next time.
#
# q35 has no IDE controller, only AHCI, so there is nothing at the ports the
# driver knows. `isa-ide` hangs a legacy controller off the LPC bridge q35
# already has, which is how the machine stays exactly the one M1 through M22
# were tested on.
DISK := $(BUILD)/me-os-disk.img
DISK_MB := 4

$(DISK):
	@mkdir -p $(BUILD)
	truncate -s $(DISK_MB)M $@
	@echo "made a $(DISK_MB) MB disk at $@. ME OS saves its filesystem here."

QEMU_DISK := -drive file=$(DISK),format=raw,if=none,id=medisk \
             -device isa-ide,id=meide -device ide-hd,drive=medisk,bus=meide.0

QEMU_COMMON := -machine q35 -m 512M -cdrom $(ISO) -boot d \
               -drive if=pflash,unit=0,format=raw,readonly=on,file=$(OVMF_CODE) \
               -drive if=pflash,unit=1,format=raw,file=$(OVMF_LOCAL) \
               $(QEMU_DISK) \
               -no-reboot -no-shutdown

run: $(ISO) $(OVMF_LOCAL) $(DISK)
	@test -n "$(OVMF_CODE)" || { echo "no OVMF firmware found. Run make check-tools." >&2; exit 1; }
	$(QEMU) $(QEMU_COMMON) -debugcon stdio

# The same thing under a name that says which image is booting, so there is no
# question of whether what ran came from $(ISO) or from somewhere else.
run-iso: run

# The other half of the hybrid image. No OVMF, so QEMU's own SeaBIOS boots it,
# which is the path VirtualBox takes by default and the one a real machine takes
# when its firmware is set to legacy.
run-bios: $(ISO) $(DISK)
	$(QEMU) -machine q35 -m 512M -cdrom $(ISO) -boot d $(QEMU_DISK) \
		-no-reboot -no-shutdown -debugcon stdio

# Same, with the kernel log on the terminal through the serial port.
run-serial: $(ISO) $(OVMF_LOCAL) $(DISK)
	$(QEMU) $(QEMU_COMMON) -serial stdio

# The same ISO in VirtualBox, in a machine of its own. The script needs no root,
# writes to no real disk, and refuses to act on any machine not named ME-OS.
run-vbox: $(ISO)
	scripts/vbox.sh run

vbox-capture: $(ISO)
	scripts/vbox.sh capture

vbox-remove:
	scripts/vbox.sh remove

# Builds the ISO twice from clean and compares the two files.
#
# Reproducibility is a claim, so it is checked rather than asserted. Everything
# that would otherwise vary is pinned: the timestamps, the GPT identifier, the
# MBR disk identifier, and the build directory, which is mapped out of the
# binary by -ffile-prefix-map.
.PHONY: check-reproducible
check-reproducible:
	$(MAKE) clean
	$(MAKE) $(ISO)
	cp $(ISO) $(BUILD)/first.iso.check
	mv $(BUILD)/first.iso.check $(BUILD)/../first-iso-check.tmp
	$(MAKE) clean
	$(MAKE) $(ISO)
	@mkdir -p $(BUILD)
	@mv $(BUILD)/../first-iso-check.tmp $(BUILD)/first.iso.check
	@cmp $(BUILD)/first.iso.check $(ISO) \
		&& echo "reproducible: two clean builds are byte for byte identical" \
		|| { echo "NOT reproducible: the two builds differ" >&2; exit 1; }
	@$(RM) $(BUILD)/first.iso.check

# Framebuffer clipping checked on the host, with guard regions around a fake
# framebuffer. Catches an out of bounds write without booting anything.
# The filesystem is five files now, and three test programs link all of them.
# The shell is six files: cutting a line up, the machine's own answers, the
# dispatcher and its pipeline, files, text filters, and somewhere to write.
CMD_SRCS := kernel/src/cmd.c kernel/src/cmdline.c kernel/src/cmdinfo.c \
            kernel/src/cmdfs.c kernel/src/cmdtext.c kernel/src/cmdsort.c \
            kernel/src/cmdout.c kernel/src/cmdrun.c

VFS_SRCS := kernel/src/vfs.c kernel/src/vfspath.c kernel/src/vfstree.c \
            kernel/src/vfsfile.c kernel/src/vfsmove.c kernel/src/vfsblock.c

HOST_TEST_FLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wshadow -Ikernel/include

$(BUILD)/fb_bounds_test: tests/fb_bounds_test.c kernel/src/fb.c kernel/src/font.c \
                         kernel/src/surface.c kernel/src/region.c \
                         $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) \
		tests/fb_bounds_test.c kernel/src/fb.c kernel/src/font.c kernel/src/surface.c \
		kernel/src/region.c -o $@

# Rectangle arithmetic on its own. Every dirty region the compositor and the
# framebuffer act on is built out of these, so an off by one here writes outside
# a window rather than merely drawing the wrong thing.
$(BUILD)/region_test: tests/region_test.c kernel/src/region.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/region_test.c kernel/src/region.c -o $@

# The tiling layout is pure arithmetic, so the property that matters, that no
# two visible windows ever share a pixel, is checked without a framebuffer.
$(BUILD)/tile_test: tests/tile_test.c kernel/src/tile.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/tile_test.c kernel/src/tile.c -o $@

# The shell only draws, so the checks are that it stays inside what it was
# given and that a click is matched against the rectangle that was painted.
$(BUILD)/shell_test: tests/shell_test.c kernel/src/shell.c kernel/src/surface.c \
                     kernel/src/font.c kernel/src/region.c kernel/src/tile.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/shell_test.c kernel/src/shell.c \
		kernel/src/surface.c kernel/src/font.c kernel/src/region.c \
		kernel/src/tile.c -o $@

# The terminal, its line editor and every command it answers. No framebuffer
# and no emulator: what is worth checking here is the scroll that drops a line,
# the backspace that must not eat the prompt, and the sizes a person reads.
$(BUILD)/term_test: tests/term_test.c kernel/src/term.c kernel/src/termback.c \
                    $(CMD_SRCS) $(VFS_SRCS) kernel/src/surface.c \
                    kernel/src/font.c kernel/src/region.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/term_test.c kernel/src/term.c \
		kernel/src/termback.c \
		$(CMD_SRCS) $(VFS_SRCS) \
		kernel/src/surface.c kernel/src/font.c kernel/src/region.c -o $@

# The editor on its own: insert in the middle, split a line, join two, and the
# round trip that saving a file depends on.
$(BUILD)/editor_test: tests/editor_test.c kernel/src/editor.c kernel/src/surface.c \
                      kernel/src/font.c kernel/src/region.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/editor_test.c kernel/src/editor.c \
		kernel/src/surface.c kernel/src/font.c kernel/src/region.c -o $@

# The clock's decoding, against values from the chip's datasheet. The chip
# itself is not read: a development machine is not the machine that boots.
$(BUILD)/rtc_test: tests/rtc_test.c kernel/src/rtc.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) -DME_NO_CMOS tests/rtc_test.c kernel/src/rtc.c -o $@

# The filesystem on its own. Path resolution, the root that cannot be climbed
# out of, and the working directory that must not be deleted from under you.
$(BUILD)/vfs_test: tests/vfs_test.c $(VFS_SRCS) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/vfs_test.c $(VFS_SRCS) -o $@

# CPUID unpacking, checked against registers whose answer is written down in
# the manual. The instruction itself is not run here: a host is not necessarily
# the machine the kernel boots on.
$(BUILD)/vfsdisk_test: tests/vfsdisk_test.c kernel/src/vfsdisk.c \
                       kernel/src/vfsdisk_format.c kernel/src/vfsdisk_check.c \
                       kernel/src/disk.c $(VFS_SRCS) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/vfsdisk_test.c kernel/src/vfsdisk.c \
		kernel/src/vfsdisk_format.c kernel/src/vfsdisk_check.c \
		kernel/src/disk.c $(VFS_SRCS) -o $@

$(BUILD)/cpu_test: tests/cpu_test.c kernel/src/cpu.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) -DME_NO_CPUID tests/cpu_test.c kernel/src/cpu.c -o $@

# The whole environment: real window objects, real tiling, real surfaces, and
# the invariant that no two visible tiles ever share a pixel.
$(BUILD)/desktop_test: tests/desktop_test.c kernel/src/desktop.c kernel/src/shell.c \
                       kernel/src/tile.c kernel/src/window.c kernel/src/event.c \
                       kernel/src/surface.c kernel/src/font.c kernel/src/region.c \
                       kernel/src/compositor.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/desktop_test.c kernel/src/desktop.c \
		kernel/src/shell.c kernel/src/tile.c kernel/src/window.c \
		kernel/src/event.c kernel/src/surface.c kernel/src/font.c \
		kernel/src/region.c kernel/src/compositor.c -o $@

# mouse.c compiles on the host because only its pure decoding is called here.
# Nothing in this test touches a port.
$(BUILD)/pointer_test: tests/pointer_test.c kernel/src/mouse.c kernel/src/pointer.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) \
		tests/pointer_test.c kernel/src/mouse.c kernel/src/pointer.c -o $@

$(BUILD)/timer_rect_test: tests/timer_rect_test.c kernel/src/timer.c kernel/src/rect.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) \
		tests/timer_rect_test.c kernel/src/timer.c kernel/src/rect.c -o $@

$(BUILD)/calc_test: tests/calc_test.c kernel/src/calc.c kernel/src/vars.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) \
		tests/calc_test.c kernel/src/calc.c kernel/src/vars.c -o $@

$(BUILD)/vars_test: tests/vars_test.c kernel/src/vars.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/vars_test.c kernel/src/vars.c -o $@

$(BUILD)/kbd_test: tests/kbd_test.c kernel/src/kbd.c kernel/src/font.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/kbd_test.c kernel/src/kbd.c \
		kernel/src/font.c -o $@

$(BUILD)/geometry_test: tests/geometry_test.c kernel/src/geometry.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/geometry_test.c kernel/src/geometry.c -o $@ -lm

$(BUILD)/window_test: tests/window_test.c kernel/src/window.c kernel/src/event.c \
                      kernel/src/surface.c kernel/src/font.c kernel/src/region.c \
                         $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/window_test.c kernel/src/window.c \
		kernel/src/event.c kernel/src/surface.c kernel/src/font.c \
		kernel/src/region.c -o $@

$(BUILD)/surface_test: tests/surface_test.c kernel/src/surface.c kernel/src/font.c \
                       kernel/src/window.c kernel/src/compositor.c kernel/src/cursor.c \
                       kernel/src/event.c kernel/src/region.c \
                         $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/surface_test.c kernel/src/surface.c \
		kernel/src/font.c kernel/src/window.c kernel/src/compositor.c \
		kernel/src/cursor.c kernel/src/event.c kernel/src/region.c -o $@

$(BUILD)/event_test: tests/event_test.c kernel/src/event.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(HOST_TEST_FLAGS) tests/event_test.c kernel/src/event.c -o $@

test-unit: $(BUILD)/fb_bounds_test $(BUILD)/pointer_test $(BUILD)/timer_rect_test \
           $(BUILD)/calc_test $(BUILD)/vars_test $(BUILD)/kbd_test \
           $(BUILD)/geometry_test $(BUILD)/window_test $(BUILD)/surface_test \
           $(BUILD)/event_test $(BUILD)/region_test $(BUILD)/tile_test \
           $(BUILD)/shell_test $(BUILD)/desktop_test \
           $(BUILD)/term_test $(BUILD)/cpu_test $(BUILD)/vfs_test \
           $(BUILD)/editor_test $(BUILD)/rtc_test $(BUILD)/vfsdisk_test
	$(BUILD)/fb_bounds_test
	$(BUILD)/pointer_test
	$(BUILD)/timer_rect_test
	$(BUILD)/calc_test
	$(BUILD)/vars_test
	$(BUILD)/kbd_test
	$(BUILD)/geometry_test
	$(BUILD)/window_test
	$(BUILD)/surface_test
	$(BUILD)/event_test
	$(BUILD)/region_test
	$(BUILD)/tile_test
	$(BUILD)/shell_test
	$(BUILD)/desktop_test
	$(BUILD)/vfs_test
	$(BUILD)/vfsdisk_test
	$(BUILD)/editor_test
	$(BUILD)/rtc_test
	$(BUILD)/term_test
	$(BUILD)/cpu_test

# Headless boot that captures the screen and checks it, no display needed.
test: $(ISO) $(OVMF_LOCAL)
	BUILD_DIR="$(BUILD)" OVMF_CODE="$(OVMF_CODE)" OVMF_VARS_LOCAL="$(OVMF_LOCAL)" QEMU="$(QEMU)" \
		scripts/boot-capture.sh
	BUILD_DIR="$(BUILD)" python3 tests/check_boot.py

check: check-tools all test-unit test

clean:
	$(RM) -r $(BUILD)

distclean: clean
	$(RM) -r $(LIMINE_DIR)

-include $(DEPS)
