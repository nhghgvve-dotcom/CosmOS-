#ifndef COSMOS_NET_H
#define COSMOS_NET_H
#include <stdint.h>
#include <stddef.h>

/* Small, allocation-free Ethernet/IPv4 protocol primitives.
   These routines build and validate packets; they do not claim to provide
   a live network until an initialized NIC can DMA frames to/from them. */
#define COSMOS_ETH_ADDR_LEN 6
#define COSMOS_ETH_HEADER_LEN 14
#define COSMOS_IPV4_HEADER_LEN 20
#define COSMOS_UDP_HEADER_LEN 8
#define COSMOS_ETH_TYPE_IPV4 0x0800
#define COSMOS_ETH_TYPE_ARP  0x0806
#define COSMOS_IPPROTO_ICMP 1
#define COSMOS_IPPROTO_TCP  6
#define COSMOS_IPPROTO_UDP  17

typedef struct {
 uint8_t mac[6];
 uint32_t ipv4; /* Stored in network byte order. */
 uint32_t expires;
 int valid;
} cosmos_arp_entry;

typedef struct {
 uint32_t rx_frames,tx_frames,rx_dropped,tx_dropped;
 uint32_t ipv4_rx,arp_rx,udp_rx,tcp_rx;
 uint32_t ipv4_address,netmask,gateway,dns_server;
 int link_up,dhcp_bound;
} cosmos_net_state;

static cosmos_net_state cosmos_net;
static cosmos_arp_entry cosmos_arp_cache[16];

static inline uint16_t cosmos_net_read_be16(const uint8_t *p) {
 return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}
static inline uint32_t cosmos_net_read_be32(const uint8_t *p) {
 return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static inline void cosmos_net_write_be16(uint8_t *p,uint16_t v) {
 p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;
}
static inline void cosmos_net_write_be32(uint8_t *p,uint32_t v) {
 p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static uint16_t cosmos_net_checksum(const void *data,size_t length) {
 const uint8_t *p=(const uint8_t*)data;uint32_t sum=0;
 while(length>1){sum+=cosmos_net_read_be16(p);p+=2;length-=2;}
 if(length)sum+=(uint16_t)((uint16_t)p[0]<<8);
 while(sum>>16)sum=(sum&0xFFFFu)+(sum>>16);
 return (uint16_t)~sum;
}
static void cosmos_net_init(void) {
 for(size_t i=0;i<sizeof(cosmos_net);i++)((uint8_t*)&cosmos_net)[i]=0;
 for(size_t i=0;i<16;i++)cosmos_arp_cache[i].valid=0;
}
static int cosmos_net_ethernet_view(const uint8_t *frame,size_t length,
 const uint8_t **payload,uint16_t *type) {
 if(!frame||length<COSMOS_ETH_HEADER_LEN||!payload||!type)return 0;
 *type=cosmos_net_read_be16(frame+12);*payload=frame+COSMOS_ETH_HEADER_LEN;
 return 1;
}
static int cosmos_net_arp_remember(uint32_t ipv4,const uint8_t mac[6],uint32_t now) {
 if(!mac||ipv4==0)return 0;
 size_t slot=16;
 for(size_t i=0;i<16;i++){
  if(cosmos_arp_cache[i].valid&&cosmos_arp_cache[i].ipv4==ipv4){slot=i;break;}
  if(!cosmos_arp_cache[i].valid&&slot==16)slot=i;
 }
 if(slot==16)slot=(size_t)(ipv4%16u);
 cosmos_arp_cache[slot].ipv4=ipv4;cosmos_arp_cache[slot].expires=now+300;
 for(size_t i=0;i<6;i++)cosmos_arp_cache[slot].mac[i]=mac[i];
 cosmos_arp_cache[slot].valid=1;return 1;
}
static const uint8_t *cosmos_net_arp_lookup(uint32_t ipv4,uint32_t now) {
 for(size_t i=0;i<16;i++)if(cosmos_arp_cache[i].valid){
  if((int32_t)(cosmos_arp_cache[i].expires-now)<=0){cosmos_arp_cache[i].valid=0;continue;}
  if(cosmos_arp_cache[i].ipv4==ipv4)return cosmos_arp_cache[i].mac;
 }
 return NULL;
}
static int cosmos_net_ipv4_view(const uint8_t *packet,size_t length,
 const uint8_t **payload,size_t *payload_len,uint8_t *protocol,
 uint32_t *source,uint32_t *destination) {
 if(!packet||length<COSMOS_IPV4_HEADER_LEN||(packet[0]>>4)!=4)return 0;
 size_t hlen=(size_t)(packet[0]&15u)*4u;
 if(hlen<COSMOS_IPV4_HEADER_LEN||hlen>length)return 0;
 uint16_t total=cosmos_net_read_be16(packet+2);
 if(total<hlen||total>length||cosmos_net_checksum(packet,hlen)!=0)return 0;
 if(cosmos_net_read_be16(packet+6)&0x3FFFu)return 0; /* no fragmented datagrams yet */
 if(payload)*payload=packet+hlen;
 if(payload_len)*payload_len=total-hlen;
 if(protocol)*protocol=packet[9];
 if(source)*source=cosmos_net_read_be32(packet+12);
 if(destination)*destination=cosmos_net_read_be32(packet+16);
 return 1;
}
static int cosmos_net_udp_view(const uint8_t *packet,size_t length,
 uint16_t *src_port,uint16_t *dst_port,const uint8_t **payload,size_t *payload_len) {
 if(!packet||length<COSMOS_UDP_HEADER_LEN)return 0;
 uint16_t n=cosmos_net_read_be16(packet+4);
 if(n<COSMOS_UDP_HEADER_LEN||n>length)return 0;
 if(src_port)*src_port=cosmos_net_read_be16(packet);
 if(dst_port)*dst_port=cosmos_net_read_be16(packet+2);
 if(payload)*payload=packet+COSMOS_UDP_HEADER_LEN;
 if(payload_len)*payload_len=n-COSMOS_UDP_HEADER_LEN;
 return 1;
}
static size_t cosmos_net_build_arp_request(uint8_t *out,size_t capacity,
 const uint8_t local_mac[6],uint32_t local_ip,uint32_t target_ip) {
 if(!out||!local_mac||capacity<42)return 0;
 for(size_t i=0;i<6;i++)out[i]=0xFF;
 for(size_t i=0;i<6;i++)out[6+i]=local_mac[i];
 cosmos_net_write_be16(out+12,COSMOS_ETH_TYPE_ARP);
 cosmos_net_write_be16(out+14,1);cosmos_net_write_be16(out+16,COSMOS_ETH_TYPE_IPV4);
 out[18]=6;out[19]=4;cosmos_net_write_be16(out+20,1);
 for(size_t i=0;i<6;i++)out[22+i]=local_mac[i];
 cosmos_net_write_be32(out+28,local_ip);
 for(size_t i=0;i<6;i++)out[32+i]=0;
 cosmos_net_write_be32(out+38,target_ip);
 return 42;
}
static size_t cosmos_net_build_dhcp_discover(uint8_t *out,size_t capacity,
 const uint8_t mac[6],uint32_t xid) {
 if(!out||!mac||capacity<300)return 0;
 for(size_t i=0;i<300;i++)out[i]=0;
 out[0]=1;out[1]=1;out[2]=6;cosmos_net_write_be32(out+4,xid);
 out[10]=0x80; /* broadcast flag */
 for(size_t i=0;i<6;i++)out[28+i]=mac[i];
 cosmos_net_write_be32(out+236,0x63825363u);
 out[240]=53;out[241]=1;out[242]=1; /* DHCP message type: discover */
 out[243]=55;out[244]=4;out[245]=1;out[246]=3;out[247]=6;out[248]=15;
 out[249]=255;
 return 300;
}
static size_t cosmos_net_build_dns_a_query(uint8_t *out,size_t capacity,
 uint16_t id,const char *hostname) {
 if(!out||!hostname||capacity<17)return 0;
 for(size_t i=0;i<12;i++)out[i]=0;
 cosmos_net_write_be16(out,id);cosmos_net_write_be16(out+2,0x0100);
 cosmos_net_write_be16(out+4,1);
 size_t n=12,i=0;
 while(hostname[i]){
  size_t start=i,label=0;
  while(hostname[i]&&hostname[i]!='.'){i++;label++;}
  if(!label||label>63||n+label+2>capacity)return 0;
  out[n++]=(uint8_t)label;
  for(size_t j=start;j<i;j++)out[n++]=(uint8_t)hostname[j];
  if(hostname[i]=='.')i++;
 }
 if(n+5>capacity)return 0;
 out[n++]=0;cosmos_net_write_be16(out+n,1);n+=2; /* A */
 cosmos_net_write_be16(out+n,1);n+=2; /* IN */
 return n;
}
#endif
