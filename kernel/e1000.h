#ifndef COSMOS_E1000_H
#define COSMOS_E1000_H
#include <stdint.h>
#include <stddef.h>
#include "pci.h"
#include "memory.h"
#include "net.h"

/* Polled Intel 82540EM/82545EM driver for VMware's E1000 virtual NIC.
   DMA rings and packet buffers are allocated from Limine USABLE memory. */
#define COSMOS_E1000_RING 8u
#define COSMOS_E1000_BUF 2048u
#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_EERD 0x0014
#define E1000_ICR 0x00C0
#define E1000_RCTL 0x0100
#define E1000_TCTL 0x0400
#define E1000_TIPG 0x0410
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_RAL 0x5400
#define E1000_RAH 0x5404

typedef struct __attribute__((packed)){
 uint64_t address;uint16_t length;uint16_t checksum;
 uint8_t status;uint8_t errors;uint16_t special;
} cosmos_e1000_rx_desc;
typedef struct __attribute__((packed)){
 uint64_t address;uint16_t length;uint8_t cso;uint8_t command;
 uint8_t status;uint8_t css;uint16_t special;
} cosmos_e1000_tx_desc;

typedef struct{
 int present,initialized,link_up;
 uint8_t mac[6];
 uint64_t mmio_base;
 uint32_t device_id;
} cosmos_e1000_info;
static cosmos_e1000_info cosmos_e1000;
static volatile uint32_t *cosmos_e1000_regs=NULL;
static cosmos_e1000_rx_desc *cosmos_rx_ring=NULL;
static cosmos_e1000_tx_desc *cosmos_tx_ring=NULL;
static uint64_t cosmos_rx_ring_pa=0,cosmos_tx_ring_pa=0;
static uint64_t cosmos_rx_buf_pa[COSMOS_E1000_RING],cosmos_tx_buf_pa[COSMOS_E1000_RING];
static uint8_t *cosmos_rx_buf[COSMOS_E1000_RING],*cosmos_tx_buf[COSMOS_E1000_RING];
static uint32_t cosmos_rx_next=0,cosmos_tx_next=0;

static inline uint32_t cosmos_e1000_read(uint32_t reg){return cosmos_e1000_regs[reg/4];}
static inline void cosmos_e1000_write(uint32_t reg,uint32_t value){cosmos_e1000_regs[reg/4]=value;}
static void cosmos_e1000_probe(void){
 cosmos_e1000.present=0;cosmos_e1000.mmio_base=0;cosmos_e1000.device_id=0;
 for(uint32_t i=0;i<cosmos_pci_device_count;i++){
  const cosmos_pci_device *p=&cosmos_pci_devices[i];
  if(p->vendor_id!=0x8086||p->class_code!=0x02||p->subclass!=0x00)continue;
  if(p->device_id!=0x100E&&p->device_id!=0x100F)continue;
  uint32_t bar=cosmos_pci_config_read32(p->bus,p->device,p->function,0x10);
  if((bar&1u)||!(bar&~0xFu))continue;
  uint64_t base=(uint64_t)(bar&~0xFu);
  if(((bar>>1)&3u)==2u){
   uint32_t high=cosmos_pci_config_read32(p->bus,p->device,p->function,0x14);
   base|=(uint64_t)high<<32;
  }
  cosmos_e1000.present=1;cosmos_e1000.mmio_base=base;
  cosmos_e1000.device_id=p->device_id;
  /* Enable PCI memory decoding and bus mastering for DMA. */
  uint16_t command=cosmos_pci_config_read16(p->bus,p->device,p->function,0x04);
  cosmos_pci_config_write16(p->bus,p->device,p->function,0x04,(uint16_t)(command|0x0006u));
  return;
 }
}
static int cosmos_e1000_init(void){
 cosmos_e1000_probe();
 if(!cosmos_e1000.present||!cosmos_hhdm_offset)return 0;
 cosmos_e1000_regs=(volatile uint32_t*)cosmos_phys_to_virt(cosmos_e1000.mmio_base);
 if(!cosmos_e1000_regs)return 0;
 /* Reset device and wait for reset completion. */
 cosmos_e1000_write(E1000_CTRL,cosmos_e1000_read(E1000_CTRL)|(1u<<26));
 for(volatile uint32_t d=0;d<1000000u;d++)__asm__ volatile("pause");
 uint64_t pa=0;
 cosmos_rx_ring=(cosmos_e1000_rx_desc*)cosmos_dma_alloc_page(&cosmos_rx_ring_pa);
 cosmos_tx_ring=(cosmos_e1000_tx_desc*)cosmos_dma_alloc_page(&cosmos_tx_ring_pa);
 if(!cosmos_rx_ring||!cosmos_tx_ring)return 0;
 for(uint32_t i=0;i<COSMOS_E1000_RING;i++){
  cosmos_rx_buf[i]=(uint8_t*)cosmos_dma_alloc_page(&cosmos_rx_buf_pa[i]);
  cosmos_tx_buf[i]=(uint8_t*)cosmos_dma_alloc_page(&cosmos_tx_buf_pa[i]);
  if(!cosmos_rx_buf[i]||!cosmos_tx_buf[i])return 0;
  cosmos_rx_ring[i].address=cosmos_rx_buf_pa[i];
  cosmos_rx_ring[i].status=0;
  cosmos_tx_ring[i].address=cosmos_tx_buf_pa[i];
  cosmos_tx_ring[i].status=1; /* descriptor initially available */
 }
 /* Disable queues while programming physical descriptor addresses. */
 cosmos_e1000_write(E1000_RCTL,0);cosmos_e1000_write(E1000_TCTL,0);
 cosmos_e1000_write(E1000_RDBAL,(uint32_t)cosmos_rx_ring_pa);
 cosmos_e1000_write(E1000_RDBAH,(uint32_t)(cosmos_rx_ring_pa>>32));
 cosmos_e1000_write(E1000_RDLEN,COSMOS_E1000_RING*16);
 cosmos_e1000_write(E1000_RDH,0);cosmos_e1000_write(E1000_RDT,COSMOS_E1000_RING-1);
 cosmos_e1000_write(E1000_TDBAL,(uint32_t)cosmos_tx_ring_pa);
 cosmos_e1000_write(E1000_TDBAH,(uint32_t)(cosmos_tx_ring_pa>>32));
 cosmos_e1000_write(E1000_TDLEN,COSMOS_E1000_RING*16);
 cosmos_e1000_write(E1000_TDH,0);cosmos_e1000_write(E1000_TDT,0);
 cosmos_e1000_write(E1000_TIPG,0x0060200A);
 cosmos_e1000_write(E1000_TCTL,0x0004010A);
 cosmos_e1000_write(E1000_RCTL,0x04008002); /* RX enable, broadcast, strip CRC */
 uint32_t lo=cosmos_e1000_read(E1000_RAL),hi=cosmos_e1000_read(E1000_RAH);
 cosmos_e1000.mac[0]=(uint8_t)lo;cosmos_e1000.mac[1]=(uint8_t)(lo>>8);
 cosmos_e1000.mac[2]=(uint8_t)(lo>>16);cosmos_e1000.mac[3]=(uint8_t)(lo>>24);
 cosmos_e1000.mac[4]=(uint8_t)hi;cosmos_e1000.mac[5]=(uint8_t)(hi>>8);
 cosmos_rx_next=cosmos_tx_next=0;
 cosmos_e1000.initialized=1;
 cosmos_e1000.link_up=(cosmos_e1000_read(E1000_STATUS)&2u)!=0;
 return 1;
}
static int cosmos_e1000_send(const void *frame,size_t length){
 if(!cosmos_e1000.initialized||!frame||length<14||length>COSMOS_E1000_BUF)return 0;
 uint32_t i=cosmos_tx_next;cosmos_e1000_tx_desc *d=&cosmos_tx_ring[i];
 for(uint32_t wait=0;wait<1000000u&&!(d->status&1u);wait++)__asm__ volatile("pause");
 if(!(d->status&1u)){cosmos_net.tx_dropped++;return 0;}
 const uint8_t *src=(const uint8_t*)frame;
 for(size_t n=0;n<length;n++)cosmos_tx_buf[i][n]=src[n];
 d->length=(uint16_t)length;d->cso=0;d->css=0;d->special=0;
 d->command=0x0B;d->status=0;
 __asm__ volatile("" ::: "memory");
 cosmos_e1000_write(E1000_TDT,(i+1u)%COSMOS_E1000_RING);
 cosmos_tx_next=(i+1u)%COSMOS_E1000_RING;
 for(uint32_t wait=0;wait<1000000u&&!(d->status&1u);wait++)__asm__ volatile("pause");
 if(!(d->status&1u)){cosmos_net.tx_dropped++;return 0;}
 cosmos_net.tx_frames++;return 1;
}
static void cosmos_e1000_account_frame(const uint8_t *frame,size_t length){
 const uint8_t *payload;uint16_t type;
 if(!cosmos_net_ethernet_view(frame,length,&payload,&type))return;
 cosmos_net.rx_frames++;
 if(type==COSMOS_ETH_TYPE_ARP&&length>=42){
  if(cosmos_net_read_be16(payload)==1&&cosmos_net_read_be16(payload+2)==COSMOS_ETH_TYPE_IPV4&&payload[4]==6&&payload[5]==4){
   cosmos_net.arp_rx++;
   uint32_t sender=cosmos_net_read_be32(payload+14);
   cosmos_net_arp_remember(sender,payload+8,0);
  }
 }else if(type==COSMOS_ETH_TYPE_IPV4){
  const uint8_t *body;size_t bodylen;uint8_t proto;uint32_t src,dst;
  if(cosmos_net_ipv4_view(payload,length-14,&body,&bodylen,&proto,&src,&dst)){
   cosmos_net.ipv4_rx++;
   if(proto==COSMOS_IPPROTO_TCP)cosmos_net.tcp_rx++;
   if(proto==COSMOS_IPPROTO_UDP)cosmos_net.udp_rx++;
  }
 }
}
static void cosmos_e1000_poll(void){
 if(!cosmos_e1000.initialized)return;
 cosmos_e1000.link_up=(cosmos_e1000_read(E1000_STATUS)&2u)!=0;
 for(uint32_t work=0;work<COSMOS_E1000_RING;work++){
  uint32_t i=cosmos_rx_next;cosmos_e1000_rx_desc *d=&cosmos_rx_ring[i];
  if(!(d->status&1u))break;
  if(d->errors==0&&d->length>=14&&d->length<=COSMOS_E1000_BUF)
   cosmos_e1000_account_frame(cosmos_rx_buf[i],d->length);
  else cosmos_net.rx_dropped++;
  d->length=0;d->checksum=0;d->errors=0;d->status=0;
  __asm__ volatile("" ::: "memory");
  cosmos_e1000_write(E1000_RDT,i);
  cosmos_rx_next=(i+1u)%COSMOS_E1000_RING;
 }
}
#endif
