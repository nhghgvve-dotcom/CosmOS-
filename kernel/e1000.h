#ifndef COSMOS_E1000_H
#define COSMOS_E1000_H
#include <stdint.h>
#include "pci.h"

/* VMware commonly presents its virtual Intel 82540EM (8086:100E) or
   82545EM (8086:100F) NIC. This stage safely identifies the PCI function
   and decodes its MMIO BAR; it does not yet start DMA or send packets. */
typedef struct {
    int present;
    uint8_t bus, device, function;
    uint16_t vendor_id, device_id;
    uint64_t mmio_base;
    uint32_t mmio_size; /* Unknown until BAR sizing is implemented. */
    int bar_is_mmio;
} cosmos_e1000_info;

static cosmos_e1000_info cosmos_e1000;

static void cosmos_e1000_probe(void) {
    cosmos_e1000.present = 0;
    cosmos_e1000.mmio_base = 0;
    cosmos_e1000.mmio_size = 0;
    cosmos_e1000.bar_is_mmio = 0;

    for (uint32_t i = 0; i < cosmos_pci_device_count; ++i) {
        const cosmos_pci_device *p = &cosmos_pci_devices[i];
        if (p->vendor_id != 0x8086 || p->class_code != 0x02 ||
            p->subclass != 0x00) continue;

        /* Known Intel 8254x/8257x devices commonly used by emulators. */
        if (p->device_id != 0x100E && p->device_id != 0x100F &&
            p->device_id != 0x1010 && p->device_id != 0x1011 &&
            p->device_id != 0x1026 && p->device_id != 0x105E &&
            p->device_id != 0x10D3) continue;

        cosmos_e1000.present = 1;
        cosmos_e1000.bus = p->bus;
        cosmos_e1000.device = p->device;
        cosmos_e1000.function = p->function;
        cosmos_e1000.vendor_id = p->vendor_id;
        cosmos_e1000.device_id = p->device_id;

        /* BAR0: accept a 32-bit or 64-bit memory-mapped register window.
           Keep the address as a physical address; mapping is a later stage. */
        uint32_t bar0 = cosmos_pci_config_read32(
            p->bus, p->device, p->function, 0x10);
        if ((bar0 & 1u) == 0) {
            uint64_t base = (uint64_t)(bar0 & ~0xFu);
            if (((bar0 >> 1) & 3u) == 2u) {
                uint32_t bar1 = cosmos_pci_config_read32(
                    p->bus, p->device, p->function, 0x14);
                base |= (uint64_t)bar1 << 32;
            }
            cosmos_e1000.mmio_base = base;
            cosmos_e1000.bar_is_mmio = base != 0;
        }
        return;
    }
}
#endif
