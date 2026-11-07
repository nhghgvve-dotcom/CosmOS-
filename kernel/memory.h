#ifndef COSMOS_MEMORY_H
#define COSMOS_MEMORY_H
#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* Limine-provided direct physical map and usable-memory map. */
static volatile struct limine_hhdm_request cosmos_hhdm_request
 __attribute__((used,section(".limine_requests"))) = {
 .id=LIMINE_HHDM_REQUEST_ID,.revision=0,.response=NULL
};
static volatile struct limine_memmap_request cosmos_memmap_request
 __attribute__((used,section(".limine_requests"))) = {
 .id=LIMINE_MEMMAP_REQUEST_ID,.revision=0,.response=NULL
};
static uint64_t cosmos_hhdm_offset=0;
static struct limine_memmap_response *cosmos_memmap=NULL;
static uint64_t cosmos_memmap_index=0,cosmos_memmap_cursor=0;
static uint64_t cosmos_memory_pages_allocated=0;

static int cosmos_memory_init(void){
 cosmos_hhdm_offset=cosmos_hhdm_request.response?cosmos_hhdm_request.response->offset:0;
 cosmos_memmap=cosmos_memmap_request.response;
 cosmos_memmap_index=0;cosmos_memmap_cursor=0;cosmos_memory_pages_allocated=0;
 return cosmos_hhdm_offset!=0&&cosmos_memmap&&cosmos_memmap->entries&&cosmos_memmap->entry_count;
}
/* Bump-only page allocator: safe for early boot DMA buffers. Pages come only
   from Limine USABLE ranges, never from the kernel/modules or framebuffer. */
static void *cosmos_dma_alloc_page(uint64_t *physical){
 if(!cosmos_memmap||!cosmos_hhdm_offset)return NULL;
 while(cosmos_memmap_index<cosmos_memmap->entry_count){
  struct limine_memmap_entry *e=cosmos_memmap->entries[cosmos_memmap_index];
  if(!e||e->type!=LIMINE_MEMMAP_USABLE){cosmos_memmap_index++;cosmos_memmap_cursor=0;continue;}
  uint64_t start=e->base;
  if(start<0x100000)start=0x100000;
  if(!cosmos_memmap_cursor)cosmos_memmap_cursor=(start+4095u)&~4095ull;
  uint64_t end=e->base+e->length;
  if(cosmos_memmap_cursor<=end&&end-cosmos_memmap_cursor>=4096){
   uint64_t pa=cosmos_memmap_cursor;cosmos_memmap_cursor+=4096;
   uint8_t *v=(uint8_t*)(uintptr_t)(cosmos_hhdm_offset+pa);
   for(size_t i=0;i<4096;i++)v[i]=0;
   if(physical)*physical=pa;
   cosmos_memory_pages_allocated++;
   return v;
  }
  cosmos_memmap_index++;cosmos_memmap_cursor=0;
 }
 return NULL;
}
static inline void *cosmos_phys_to_virt(uint64_t physical){
 if(!cosmos_hhdm_offset)return NULL;
 return (void*)(uintptr_t)(cosmos_hhdm_offset+physical);
}
#endif
