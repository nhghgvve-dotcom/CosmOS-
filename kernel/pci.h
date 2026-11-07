#ifndef COSMOS_PCI_H
#define COSMOS_PCI_H
#include <stdint.h>

/* Read-only PCI configuration-space discovery (legacy x86 config mechanism #1).
   This deliberately does not modify BARs or enable bus mastering. */
typedef struct {
 uint8_t bus,device,function;
 uint16_t vendor_id,device_id;
 uint8_t class_code,subclass,prog_if,revision;
 uint8_t header_type;
} cosmos_pci_device;

static cosmos_pci_device cosmos_pci_devices[256];
static uint32_t cosmos_pci_device_count;

static inline uint32_t cosmos_pci_config_read32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){
 uint32_t address=0x80000000u|((uint32_t)bus<<16)|((uint32_t)device<<11)|((uint32_t)function<<8)|(offset&0xFCu);
 uint32_t value;
 __asm__ volatile("outl %0,%1"::"a"(address),"Nd"((uint16_t)0xCF8));
 __asm__ volatile("inl %1,%0":"=a"(value):"Nd"((uint16_t)0xCFC));
 return value;
}
static inline uint16_t cosmos_pci_config_read16(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){
 uint32_t value=cosmos_pci_config_read32(bus,device,function,offset);
 return (uint16_t)(value>>((offset&2u)*8u));
}
static void cosmos_pci_init(void){
 cosmos_pci_device_count=0;
 for(uint32_t bus=0;bus<256;bus++)for(uint32_t dev=0;dev<32;dev++){
  uint16_t vendor=cosmos_pci_config_read16((uint8_t)bus,(uint8_t)dev,0,0);
  if(vendor==0xFFFF)continue;
  uint8_t header=(uint8_t)(cosmos_pci_config_read32((uint8_t)bus,(uint8_t)dev,0,0x0C)>>16);
  uint32_t functions=(header&0x80u)?8u:1u;
  for(uint32_t fn=0;fn<functions;fn++){
   vendor=cosmos_pci_config_read16((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0);
   if(vendor==0xFFFF)continue;
   uint32_t id=cosmos_pci_config_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0);
   uint32_t classreg=cosmos_pci_config_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,8);
   uint32_t hdr=cosmos_pci_config_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0x0C);
   if(cosmos_pci_device_count<256){
    cosmos_pci_device *p=&cosmos_pci_devices[cosmos_pci_device_count++];
    p->bus=(uint8_t)bus;p->device=(uint8_t)dev;p->function=(uint8_t)fn;
    p->vendor_id=(uint16_t)id;p->device_id=(uint16_t)(id>>16);
    p->revision=(uint8_t)classreg;p->prog_if=(uint8_t)(classreg>>8);
    p->subclass=(uint8_t)(classreg>>16);p->class_code=(uint8_t)(classreg>>24);
    p->header_type=(uint8_t)(hdr>>16);
   }
  }
 }
}
#endif
