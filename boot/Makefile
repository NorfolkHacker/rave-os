AS := nasm
QEMU := qemu-system-i386
KERNEL_DIR := ../kernel
KERNEL_BIN := $(KERNEL_DIR)/kernel.bin

all: disk.img

$(KERNEL_BIN): FORCE
	$(MAKE) -C $(KERNEL_DIR)

# Sector counts/offsets used to be constants hand-copied and kept in sync
# across stage1.asm, stage2.asm, and kernel/Makefile -- flagged repeatedly
# in docs/BUILD_LOG.md as a real fragility (nothing enforced them staying
# in sync, only "remember to update all three"). This single recipe
# computes them from the actual built sizes instead:
#   1. Build kernel.bin (kernel/Makefile no longer pads or budgets it --
#      that's this file's job now) and measure it.
#   2. Assemble stage2 once with placeholder kernel-location values just
#      to measure stage2's own true size (the specific numbers plugged
#      into small equ constants don't change how many bytes the
#      instructions referencing them take, but this measures rather than
#      assumes that).
#   3. Re-assemble stage2 for real with the actual computed values, pad
#      both stage2.bin and kernel.bin up to whole sectors, assemble
#      stage1 with the real STAGE2_SECTORS, and concatenate everything.
disk.img: stage1.asm stage2.asm $(KERNEL_BIN)
	@kernel_sectors=$$(( ($$(stat -c%s $(KERNEL_BIN)) + 511) / 512 )); \
	$(AS) -f bin -D KERNEL_SECTORS=1 -D KERNEL_START_SECTOR=4 stage2.asm -o /tmp/rave_stage2_measure.bin; \
	stage2_sectors=$$(( ($$(stat -c%s /tmp/rave_stage2_measure.bin) + 511) / 512 )); \
	rm -f /tmp/rave_stage2_measure.bin; \
	kernel_start_sector=$$(( 2 + stage2_sectors )); \
	echo "disk layout: stage1=1 sector, stage2=$$stage2_sectors sector(s), kernel=$$kernel_sectors sector(s) starting at sector $$kernel_start_sector"; \
	$(AS) -f bin -D KERNEL_SECTORS=$$kernel_sectors -D KERNEL_START_SECTOR=$$kernel_start_sector stage2.asm -o stage2.bin; \
	truncate -s $$(( stage2_sectors * 512 )) stage2.bin; \
	$(AS) -f bin -D STAGE2_SECTORS=$$stage2_sectors stage1.asm -o stage1.bin; \
	truncate -s $$(( kernel_sectors * 512 )) $(KERNEL_BIN); \
	cat stage1.bin stage2.bin $(KERNEL_BIN) > disk.img

# Dedicated scratch disk for the ATA driver (kernel/ata.c) and, later, a
# real on-disk filesystem -- deliberately a second drive rather than extra
# sectors tacked onto disk.img, so filesystem sectors can never collide
# with anything the bootloader/BIOS cares about. No prerequisites, so make
# only creates it once and never truncates it again -- writes from a
# previous QEMU run (this is how the ATA self-test's persistence gets
# checked) survive across builds.
fs.img:
	dd if=/dev/zero of=fs.img bs=1M count=1

# bus=0,unit=0/1 = primary IDE channel, master/slave -- same I/O ports
# (0x1F0-0x1F7, 0x3F6) the kernel's ATA driver talks to, just a different
# drive-select bit, so no secondary-channel support is needed yet.
run: disk.img fs.img
	$(QEMU) -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1

clean:
	rm -f stage1.bin stage2.bin disk.img fs.img
	$(MAKE) -C $(KERNEL_DIR) clean

.PHONY: all run clean FORCE
