/* ATA PIO driver, primary channel only, polling (no IRQ14 handler -- see
 * ata.h). Talks to whichever drive QEMU/real hardware has wired up as the
 * primary channel's master or slave; this kernel boots off the master
 * (boot/disk.img) and uses the slave (boot/fs.img) as a second, dedicated
 * scratch disk so future filesystem sectors never risk colliding with
 * anything BIOS/the bootloader cares about. */

#include "ata.h"
#include "io.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CTRL 0x3F6 /* alternate status (read) / device control (write) */

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_FLUSH_CACHE 0xE7

/* Large enough to never trip on real hardware timing, small enough that a
 * genuinely missing/broken drive fails fast instead of hanging the kernel
 * forever -- same "report failure, don't hang" discipline as the rest of
 * this driver. */
#define ATA_TIMEOUT_ITERS 100000

/* The standard "read the alternate status port 4 times and discard" trick:
 * each read takes about 100ns on real hardware, giving the drive the ~400ns
 * settle time it needs after a drive-select change before status is
 * trustworthy. Using the alternate status port (0x3F6) rather than 0x1F7
 * matters here too -- reading 0x1F7 can have side effects on some
 * controllers (e.g. clearing a pending interrupt status), 0x3F6 never does. */
static void ata_delay_400ns(void) {
    inb(ATA_CTRL);
    inb(ATA_CTRL);
    inb(ATA_CTRL);
    inb(ATA_CTRL);
}

static int ata_wait_bsy_clear(void) {
    int i;
    for (i = 0; i < ATA_TIMEOUT_ITERS; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

/* Only valid to call once ata_wait_bsy_clear() has already succeeded. */
static int ata_wait_drq(void) {
    int i;
    for (i = 0; i < ATA_TIMEOUT_ITERS; i++) {
        unsigned char status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
    }
    return -1;
}

/* Selects the given drive (LBA28 mode) and loads the LBA/sector-count
 * registers for a one-sector transfer. Common setup shared by read/write. */
static int ata_select_and_setup(unsigned char drive, unsigned int lba) {
    if (ata_wait_bsy_clear() != 0) {
        return -1;
    }

    outb(ATA_DRIVE_HEAD, (unsigned char)(0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F)));
    ata_delay_400ns(); /* settle time after changing drive select */

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));

    return 0;
}

int ata_read_sector(unsigned char drive, unsigned int lba, void *buf512) {
    unsigned short *dst = (unsigned short *)buf512;
    int i;

    if (ata_select_and_setup(drive, lba) != 0) {
        return -1;
    }

    outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);

    if (ata_wait_bsy_clear() != 0 || ata_wait_drq() != 0) {
        return -1;
    }

    for (i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        dst[i] = inw(ATA_DATA);
    }

    return 0;
}

int ata_write_sector(unsigned char drive, unsigned int lba, const void *buf512) {
    const unsigned short *src = (const unsigned short *)buf512;
    int i;

    if (ata_select_and_setup(drive, lba) != 0) {
        return -1;
    }

    outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (ata_wait_bsy_clear() != 0 || ata_wait_drq() != 0) {
        return -1;
    }

    for (i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        outw(ATA_DATA, src[i]);
    }

    /* FLUSH CACHE forces the write out to the backing media (or QEMU's
     * backing file) rather than leaving it in the controller's write
     * cache -- the whole point of testing against a real disk instead of
     * a RAM disk is that this data needs to actually survive. */
    if (ata_wait_bsy_clear() != 0) {
        return -1;
    }
    outb(ATA_COMMAND, ATA_CMD_FLUSH_CACHE);
    if (ata_wait_bsy_clear() != 0) {
        return -1;
    }

    return 0;
}

/* The very last sector of fs.img (2048 sectors total -- see kernel/fs.c,
 * which must agree on this size) -- permanently reserved so this
 * diagnostic can never collide with a real file once the filesystem
 * (kernel/fs.c) owns the rest of the disk. Used to be LBA 100, back when
 * the whole disk was still scratch space with nothing else on it. */
#define ATA_SELFTEST_LBA 2047

const char *ata_selftest(void) {
    unsigned char write_buf[ATA_SECTOR_SIZE];
    unsigned char read_buf[ATA_SECTOR_SIZE];
    int i;

    /* Status port reads all-1s when no drive is present to pull the bus
     * low -- a floating-bus read, not a real status byte. */
    if (inb(ATA_STATUS) == 0xFF) {
        return "ATA: NO DRIVE";
    }

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        write_buf[i] = (unsigned char)(i & 0xFF);
    }

    if (ata_write_sector(ATA_DRIVE_SLAVE, ATA_SELFTEST_LBA, write_buf) != 0) {
        return "ATA: WRITE FAIL";
    }

    if (ata_read_sector(ATA_DRIVE_SLAVE, ATA_SELFTEST_LBA, read_buf) != 0) {
        return "ATA: READ FAIL";
    }

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (read_buf[i] != write_buf[i]) {
            return "ATA: MISMATCH";
        }
    }

    return "ATA: OK";
}
