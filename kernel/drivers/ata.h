#ifndef RAVEOS_ATA_H
#define RAVEOS_ATA_H

#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE 1

#define ATA_SECTOR_SIZE 512

/* Reads/writes one 512-byte sector at the given LBA28 address on the
 * primary ATA channel's given drive (ATA_DRIVE_MASTER/ATA_DRIVE_SLAVE).
 * buf/buf512 must be at least ATA_SECTOR_SIZE bytes. Returns 0 on success,
 * -1 on failure (no drive present, device-reported error, or a polling
 * loop timing out rather than hanging forever). */
int ata_read_sector(unsigned char drive, unsigned int lba, void *buf512);
int ata_write_sector(unsigned char drive, unsigned int lba, const void *buf512);

/* Writes a known pattern to a scratch sector on the slave drive, reads it
 * back, and compares. Returns a static, human-readable status string
 * ("ATA: OK", "ATA: NO DRIVE", ...) meant to be shown directly in the UI. */
const char *ata_selftest(void);

#endif
