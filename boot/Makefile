AS := nasm
QEMU := qemu-system-i386
KERNEL_DIR := ../kernel
KERNEL_BIN := $(KERNEL_DIR)/kernel.bin

# VBE mode stage2 sets at boot -- 0x112 (640x480x32, the long-standing
# default) unless overridden, e.g. `make VBE_MODE=0x115` for 800x600.
# disk.img's own prerequisites (stage1.asm/stage2.asm/$(KERNEL_BIN))
# don't change when only this variable changes, so Make won't know to
# rebuild on its own -- run `make clean` first when switching this
# between builds.
VBE_MODE := 0x112

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
	$(AS) -f bin -D KERNEL_SECTORS=1 -D KERNEL_START_SECTOR=4 -D VBE_MODE=$(VBE_MODE) stage2.asm -o /tmp/rave_stage2_measure.bin; \
	stage2_sectors=$$(( ($$(stat -c%s /tmp/rave_stage2_measure.bin) + 511) / 512 )); \
	rm -f /tmp/rave_stage2_measure.bin; \
	kernel_start_sector=$$(( 2 + stage2_sectors )); \
	echo "disk layout: stage1=1 sector, stage2=$$stage2_sectors sector(s), kernel=$$kernel_sectors sector(s) starting at sector $$kernel_start_sector, VBE_MODE=$(VBE_MODE)"; \
	$(AS) -f bin -D KERNEL_SECTORS=$$kernel_sectors -D KERNEL_START_SECTOR=$$kernel_start_sector -D VBE_MODE=$(VBE_MODE) stage2.asm -o stage2.bin; \
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
# -display sdl, not the default gtk: gtk doesn't confine/grab the pointer
# in this KDE Plasma 6/Wayland desktop, so host and guest cursor position
# drift apart and clicks land in the wrong guest-space location (see
# docs/BUILD_LOG.md / project memory, first hit and fixed for the Forth
# console back on 2026-08-03, then silently reintroduced here because this
# target never got the flag).
# -device sb16 with no audiodev property: QEMU still fully answers the
# card's I/O ports/DSP handshake/DMA/IRQ5 with no audiodev attached (a real
# card doesn't need a live speaker plugged in to respond to programming
# either) -- it just means no host audio comes out. To actually hear BEEP,
# append your own backend by hand, e.g.:
#   -audiodev pa,id=snd0 -device sb16,audiodev=snd0
# (swap "pa" for "pipewire"/"alsa"/whatever your host provides -- left out
# of the default target so `make run` never depends on a specific host
# audio server being up).
run: disk.img fs.img
	$(QEMU) -accel kvm -display sdl -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1

clean:
	rm -f stage1.bin stage2.bin disk.img fs.img
	$(MAKE) -C $(KERNEL_DIR) clean

.PHONY: all run clean FORCE
