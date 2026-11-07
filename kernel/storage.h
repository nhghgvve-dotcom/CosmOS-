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


/* MBR partition table + FAT32 support.
   Formatting is an explicit operation: never call cosmos_fat32_format()
   without a user-selected disposable virtual disk and a clear confirmation. */
#define COSMOS_FAT32_MAX_PARTITIONS 4
#define COSMOS_FAT32_SECTOR_SIZE 512
typedef struct {
    uint8_t index, type;
    uint32_t start_lba, sectors;
    uint8_t bootable;
} cosmos_partition;
typedef struct {
    uint8_t device;
    uint32_t partition_lba, total_sectors, fat_lba, fat_sectors;
    uint32_t data_lba, root_cluster, cluster_count;
    uint8_t sectors_per_cluster, fat_count;
    uint16_t reserved_sectors;
    uint32_t fsinfo_sector;
    uint8_t mounted;
} cosmos_fat32_volume;

static cosmos_partition cosmos_partitions[COSMOS_FAT32_MAX_PARTITIONS];
static cosmos_fat32_volume cosmos_volumes[4];

static uint16_t cosmos_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t cosmos_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void cosmos_put16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void cosmos_put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static int cosmos_partition_scan(uint8_t device, cosmos_partition *out, int capacity) {
    uint8_t sector[512];
    if (!out || capacity <= 0 || !cosmos_storage_read(device, 0, sector)) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    int count=0;
    for (int i=0;i<4 && count<capacity;i++) {
        const uint8_t *e=&sector[446+i*16];
        uint32_t start=cosmos_le32(e+8), size=cosmos_le32(e+12);
        if (e[4] && size && start < cosmos_ata_devices[device].sectors &&
            size <= cosmos_ata_devices[device].sectors-start) {
            out[count].index=(uint8_t)i; out[count].type=e[4];
            out[count].bootable=(uint8_t)(e[0]==0x80);
            out[count].start_lba=start; out[count].sectors=size; count++;
        }
    }
    return count;
}

/* Create one MBR FAT32-LBA partition using a single-sector atomic-ish
   sequence. This modifies the target disk and must only follow confirmation. */
static int cosmos_partition_create_fat32(uint8_t device, uint32_t start_lba,
                                         uint32_t sectors) {
    uint8_t mbr[512];
    if (device>=4 || !cosmos_ata_devices[device].present || start_lba<2048 ||
        sectors<131072 || start_lba>=cosmos_ata_devices[device].sectors ||
        sectors>cosmos_ata_devices[device].sectors-start_lba ||
        !cosmos_storage_read(device,0,mbr)) return 0;
    if (mbr[510]!=0x55 || mbr[511]!=0xAA) {
        for(int i=446;i<510;i++) mbr[i]=0;
        mbr[510]=0x55; mbr[511]=0xAA;
    }
    for(int i=0;i<4;i++) if(mbr[446+i*16+4]!=0) return 0;
    uint8_t *e=&mbr[446];
    e[0]=0; e[1]=0xFE; e[2]=0xFF; e[3]=0xFF; e[4]=0x0C;
    e[5]=0xFE; e[6]=0xFF; e[7]=0xFF;
    cosmos_put32(e+8,start_lba); cosmos_put32(e+12,sectors);
    return cosmos_storage_write(device,0,mbr);
}

/* Format FAT32 with 512-byte sectors, 8-sector clusters, two FATs.
   Explicit call only; this overwrites the selected partition's contents. */
static int cosmos_fat32_format(uint8_t device, uint32_t start_lba, uint32_t sectors) {
    if(device>=4 || !cosmos_ata_devices[device].present || start_lba<1 ||
       sectors<131072 || start_lba>=cosmos_ata_devices[device].sectors ||
       sectors>cosmos_ata_devices[device].sectors-start_lba) return 0;
    const uint32_t reserved=32, spc=8;
    uint32_t fat_sectors=1, clusters=0;
    for(int n=0;n<8;n++) {
        if(sectors<=reserved+2*fat_sectors) return 0;
        clusters=(sectors-reserved-2*fat_sectors)/spc;
        uint32_t need=((clusters+2)*4+511)/512;
        if(need==fat_sectors) break;
        fat_sectors=need;
    }
    clusters=(sectors-reserved-2*fat_sectors)/spc;
    if(clusters<65525 || clusters>0x0FFFFFF5u) return 0;
    uint8_t b[512];
    for(int i=0;i<512;i++) b[i]=0;
    b[0]=0xEB;b[1]=0x58;b[2]=0x90;
    b[3]='C';b[4]='O';b[5]='S';b[6]='M';b[7]='O';b[8]='S';b[9]=' ';b[10]=' ';
    cosmos_put16(b+11,512); b[13]=(uint8_t)spc; cosmos_put16(b+14,(uint16_t)reserved);
    b[16]=2; cosmos_put16(b+17,0); cosmos_put16(b+19,0); b[21]=0xF8;
    cosmos_put16(b+22,0); cosmos_put16(b+24,63); cosmos_put16(b+26,255);
    cosmos_put32(b+28,start_lba); cosmos_put32(b+32,sectors);
    cosmos_put32(b+36,fat_sectors); cosmos_put16(b+40,0);
    cosmos_put16(b+42,0); cosmos_put32(b+44,2); cosmos_put16(b+48,1);
    cosmos_put16(b+50,6); b[64]=0x80;b[66]=0x29;
    cosmos_put32(b+67,0x434F534D); b[71]='C';b[72]='O';b[73]='S';b[74]='M';b[75]='O';b[76]='S';
    b[82]='F';b[83]='A';b[84]='T';b[85]='3';b[86]='2';b[87]=' ';b[88]=' ';
    b[510]=0x55;b[511]=0xAA;
    if(!cosmos_storage_write(device,start_lba,b) ||
       !cosmos_storage_write(device,start_lba+6,b)) return 0;
    for(int i=0;i<512;i++) b[i]=0;
    cosmos_put32(b,0x41615252); cosmos_put32(b+484,0x61417272);
    cosmos_put32(b+488,clusters-1); cosmos_put32(b+492,3);
    cosmos_put32(b+508,0xAA550000);
    if(!cosmos_storage_write(device,start_lba+1,b) ||
       !cosmos_storage_write(device,start_lba+7,b)) return 0;
    for(int i=0;i<512;i++) b[i]=0;
    cosmos_put32(b,0x0FFFFFF8); cosmos_put32(b+4,0x0FFFFFFF);
    cosmos_put32(b+8,0x0FFFFFFF);
    if(!cosmos_storage_write(device,start_lba+reserved,b) ||
       !cosmos_storage_write(device,start_lba+reserved+fat_sectors,b)) return 0;
    return 1;
}

static int cosmos_fat32_mount(uint8_t device, uint32_t partition_lba,
                              cosmos_fat32_volume *v) {
    uint8_t b[512];
    if(!v || device>=4 || !cosmos_ata_devices[device].present ||
       partition_lba>=cosmos_ata_devices[device].sectors ||
       !cosmos_storage_read(device,partition_lba,b) ||
       b[510]!=0x55 || b[511]!=0xAA) return 0;
    uint16_t bytes=cosmos_le16(b+11), reserved=cosmos_le16(b+14);
    uint8_t spc=b[13], fats=b[16];
    uint32_t total=cosmos_le16(b+19); if(!total) total=cosmos_le32(b+32);
    uint32_t fat=cosmos_le32(b+36), root=cosmos_le32(b+44);
    uint16_t fsinfo=cosmos_le16(b+48);
    if(bytes!=512 || !spc || (spc&(spc-1)) || spc>128 || !reserved ||
       !fats || fats>2 || !fat || root<2 || total<65536 ||
       partition_lba+total>cosmos_ata_devices[device].sectors) return 0;
    uint32_t data_rel=(uint32_t)reserved+(uint32_t)fats*fat;
    if(data_rel>=total) return 0;
    uint32_t clusters=(total-data_rel)/spc;
    if(clusters<65525 || clusters>0x0FFFFFF5u) return 0;
    v->device=device;v->partition_lba=partition_lba;v->total_sectors=total;
    v->fat_lba=partition_lba+reserved;v->fat_sectors=fat;
    v->data_lba=partition_lba+data_rel;v->root_cluster=root;
    v->cluster_count=clusters;v->sectors_per_cluster=spc;
    v->fat_count=fats;v->reserved_sectors=reserved;v->fsinfo_sector=fsinfo;
    v->mounted=1;return 1;
}
static int cosmos_fat32_read_cluster(cosmos_fat32_volume *v,uint32_t cluster,void *buffer) {
    if(!v||!v->mounted||!buffer||cluster<2||cluster>=v->cluster_count+2) return 0;
    uint8_t *p=(uint8_t*)buffer;
    uint32_t lba=v->data_lba+(cluster-2)*v->sectors_per_cluster;
    for(uint32_t i=0;i<v->sectors_per_cluster;i++)
        if(!cosmos_storage_read(v->device,lba+i,p+i*512)) return 0;
    return 1;
}
static int cosmos_fat32_write_cluster(cosmos_fat32_volume *v,uint32_t cluster,const void *buffer) {
    if(!v||!v->mounted||!buffer||cluster<2||cluster>=v->cluster_count+2) return 0;
    const uint8_t *p=(const uint8_t*)buffer;
    uint32_t lba=v->data_lba+(cluster-2)*v->sectors_per_cluster;
    for(uint32_t i=0;i<v->sectors_per_cluster;i++)
        if(!cosmos_storage_write(v->device,lba+i,p+i*512)) return 0;
    return 1;
}
static int cosmos_fat32_next_cluster(cosmos_fat32_volume *v,uint32_t cluster,uint32_t *next) {
    uint8_t b[512]; if(!v||!v->mounted||!next||cluster<2||cluster>=v->cluster_count+2) return 0;
    uint32_t off=cluster*4, lba=v->fat_lba+off/512;
    if(!cosmos_storage_read(v->device,lba,b)) return 0;
    *next=cosmos_le32(b+(off%512))&0x0FFFFFFF;
    return 1;
}
