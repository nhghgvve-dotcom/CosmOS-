#ifndef COSMOS_STORAGE_H
#define COSMOS_STORAGE_H
#include <stdint.h>

/* Legacy ATA PIO support for IDE disks (including VMware IDE virtual disks).
   This driver intentionally does not probe or write SATA/SCSI/NVMe devices. */
typedef struct {
    uint16_t io;
    uint8_t drive;
    uint8_t present;
    uint32_t sectors;
    char model[41];
} cosmos_ata_device;

static cosmos_ata_device cosmos_ata_devices[4];
static inline void ata_out8(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t ata_in8(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void ata_out16(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint16_t ata_in16(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void ata_delay(uint16_t ctrl) { (void)ata_in8(ctrl); (void)ata_in8(ctrl); (void)ata_in8(ctrl); (void)ata_in8(ctrl); }
static int ata_wait(uint16_t io, int want_drq) {
    for (uint32_t i = 0; i < 2000000u; ++i) {
        uint8_t s = ata_in8(io + 7);
        if (s == 0 || s == 0xFF) return 0;
        if (s & 0x01) return 0;
        if (!(s & 0x80) && (!want_drq || (s & 0x08))) return 1;
    }
    return 0;
}
static int ata_identify(uint16_t io, uint8_t drive, cosmos_ata_device *out) {
    uint16_t ctrl = (uint16_t)(io == 0x1F0 ? 0x3F6 : 0x376);
    ata_out8(io + 6, (uint8_t)(0xA0 | (drive << 4)));
    ata_delay(ctrl);
    ata_out8(io + 2, 0); ata_out8(io + 3, 0);
    ata_out8(io + 4, 0); ata_out8(io + 5, 0);
    ata_out8(io + 7, 0xEC);
    uint8_t s = ata_in8(io + 7);
    if (s == 0 || s == 0xFF) return 0;
    if (!ata_wait(io, 1)) return 0;
    uint16_t id[256];
    for (int i = 0; i < 256; ++i) id[i] = ata_in16(io);
    /* LBA28 capacity is sufficient for the first IDE virtual-disk milestone. */
    uint32_t sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (!sectors) return 0;
    out->io = io; out->drive = drive; out->present = 1; out->sectors = sectors;
    for (int i = 0; i < 20; ++i) {
        out->model[i * 2] = (char)(id[27 + i] >> 8);
        out->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    out->model[40] = 0;
    for (int i = 39; i >= 0 && out->model[i] == ' '; --i) out->model[i] = 0;
    return 1;
}
static int cosmos_storage_init(void) {
    static const uint16_t ports[2] = {0x1F0, 0x170};
    int count = 0;
    for (int c = 0; c < 2; ++c) for (uint8_t d = 0; d < 2; ++d) {
        cosmos_ata_device *dev = &cosmos_ata_devices[c * 2 + d];
        if (ata_identify(ports[c], d, dev)) ++count;
    }
    return count;
}
static int cosmos_storage_read(uint8_t device, uint32_t lba, void *buffer) {
    if (device >= 4 || !cosmos_ata_devices[device].present || lba >= cosmos_ata_devices[device].sectors || lba >= 0x10000000u || !buffer) return 0;
    cosmos_ata_device *dev = &cosmos_ata_devices[device];
    uint16_t io = dev->io, ctrl = (uint16_t)(io == 0x1F0 ? 0x3F6 : 0x376);
    ata_out8(io + 6, (uint8_t)(0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F)));
    ata_out8(io + 2, 1); ata_out8(io + 3, (uint8_t)lba);
    ata_out8(io + 4, (uint8_t)(lba >> 8)); ata_out8(io + 5, (uint8_t)(lba >> 16));
    ata_out8(io + 7, 0x20); ata_delay(ctrl);
    if (!ata_wait(io, 1)) return 0;
    uint16_t *dst = (uint16_t *)buffer;
    for (int i = 0; i < 256; ++i) dst[i] = ata_in16(io);
    return 1;
}
static int cosmos_storage_write(uint8_t device, uint32_t lba, const void *buffer) {
    if (device >= 4 || !cosmos_ata_devices[device].present || lba >= cosmos_ata_devices[device].sectors || lba >= 0x10000000u || !buffer) return 0;
    cosmos_ata_device *dev = &cosmos_ata_devices[device];
    uint16_t io = dev->io, ctrl = (uint16_t)(io == 0x1F0 ? 0x3F6 : 0x376);
    ata_out8(io + 6, (uint8_t)(0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F)));
    ata_out8(io + 2, 1); ata_out8(io + 3, (uint8_t)lba);
    ata_out8(io + 4, (uint8_t)(lba >> 8)); ata_out8(io + 5, (uint8_t)(lba >> 16));
    ata_out8(io + 7, 0x30); ata_delay(ctrl);
    if (!ata_wait(io, 1)) return 0;
    const uint16_t *src = (const uint16_t *)buffer;
    for (int i = 0; i < 256; ++i) ata_out16(io, src[i]);
    ata_out8(io + 7, 0xE7);
    return ata_wait(io, 0);
}
#endif
