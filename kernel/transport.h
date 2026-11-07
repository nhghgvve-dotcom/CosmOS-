#ifndef COSMOS_TRANSPORT_H
#define COSMOS_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#include "net.h"

/* Allocation-free transport parsing/building primitives.
   These are not a live TCP/IP stack: NIC DMA, packet dispatch, timers,
   retransmission, sockets, and TLS are separate required layers. */

#define COSMOS_TCP_MIN_HEADER 20u
#define COSMOS_TCP_FLAG_FIN 0x01u
#define COSMOS_TCP_FLAG_SYN 0x02u
#define COSMOS_TCP_FLAG_RST 0x04u
#define COSMOS_TCP_FLAG_PSH 0x08u
#define COSMOS_TCP_FLAG_ACK 0x10u

typedef struct {
 uint16_t source_port,destination_port;
 uint32_t sequence,acknowledgment;
 uint8_t flags,header_length;
 const uint8_t *payload;
 size_t payload_length;
} cosmos_tcp_view;

static uint16_t cosmos_transport_checksum_add(uint32_t sum,const uint8_t *p,size_t n) {
 while(n>1){sum+=cosmos_net_read_be16(p);p+=2;n-=2;}
 if(n)sum+=(uint16_t)((uint16_t)p[0]<<8);
 while(sum>>16)sum=(sum&0xFFFFu)+(sum>>16);
 return (uint16_t)sum;
}
static uint16_t cosmos_tcp_checksum(uint32_t source,uint32_t destination,
 const uint8_t *segment,size_t length) {
 uint8_t pseudo[12];
 cosmos_net_write_be32(pseudo,source);
 cosmos_net_write_be32(pseudo+4,destination);
 pseudo[8]=0;pseudo[9]=COSMOS_IPPROTO_TCP;
 cosmos_net_write_be16(pseudo+10,(uint16_t)length);
 uint32_t sum=0;
 sum=cosmos_transport_checksum_add(sum,pseudo,sizeof(pseudo));
 sum=cosmos_transport_checksum_add(sum,segment,length);
 while(sum>>16)sum=(sum&0xFFFFu)+(sum>>16);
 return (uint16_t)~sum;
}
static int cosmos_tcp_view_segment(const uint8_t *segment,size_t length,
 uint32_t source,uint32_t destination,cosmos_tcp_view *out) {
 if(!segment||!out||length<COSMOS_TCP_MIN_HEADER)return 0;
 uint8_t hlen=(uint8_t)((segment[12]>>4)*4u);
 if(hlen<COSMOS_TCP_MIN_HEADER||hlen>length||length>65535u)return 0;
 if(cosmos_tcp_checksum(source,destination,segment,length)!=0)return 0;
 out->source_port=cosmos_net_read_be16(segment);
 out->destination_port=cosmos_net_read_be16(segment+2);
 out->sequence=cosmos_net_read_be32(segment+4);
 out->acknowledgment=cosmos_net_read_be32(segment+8);
 out->header_length=hlen;
 out->flags=segment[13];
 out->payload=segment+hlen;
 out->payload_length=length-hlen;
 return 1;
}
/* Builds a bare SYN segment. A connection manager must supply sequence
   numbers, track retransmissions, negotiate options, and complete the
   three-way handshake before application data can be sent. */
static size_t cosmos_tcp_build_syn(uint8_t *out,size_t capacity,
 uint32_t source,uint32_t destination,uint16_t source_port,
 uint16_t destination_port,uint32_t sequence,uint16_t window) {
 if(!out||capacity<20)return 0;
 for(size_t i=0;i<20;i++)out[i]=0;
 cosmos_net_write_be16(out,source_port);
 cosmos_net_write_be16(out+2,destination_port);
 cosmos_net_write_be32(out+4,sequence);
 out[12]=0x50;out[13]=COSMOS_TCP_FLAG_SYN;
 cosmos_net_write_be16(out+14,window);
 cosmos_net_write_be16(out+16,cosmos_tcp_checksum(source,destination,out,20));
 return 20;
}

/* Parse a DHCP reply's fixed fields and common options. */
typedef struct {
 uint32_t offered_ip,server_ip,subnet_mask,router,dns_server,lease_seconds;
 uint8_t message_type;
 int has_subnet,has_router,has_dns,has_server,has_lease;
} cosmos_dhcp_reply;
static int cosmos_dhcp_parse_reply(const uint8_t *p,size_t n,cosmos_dhcp_reply *out) {
 if(!p||!out||n<240||p[0]!=2||p[1]!=1||p[2]!=6||
    cosmos_net_read_be32(p+236)!=0x63825363u)return 0;
 for(size_t i=0;i<sizeof(*out);i++)((uint8_t*)out)[i]=0;
 out->offered_ip=cosmos_net_read_be32(p+16);
 out->server_ip=cosmos_net_read_be32(p+20);
 size_t i=240;
 while(i<n){
  uint8_t tag=p[i++];
  if(tag==255)break;
  if(tag==0)continue;
  if(i>=n)return 0;
  uint8_t len=p[i++];
  if((size_t)len>n-i)return 0;
  if(tag==53&&len==1)out->message_type=p[i];
  else if(tag==1&&len==4){out->subnet_mask=cosmos_net_read_be32(p+i);out->has_subnet=1;}
  else if(tag==3&&len>=4){out->router=cosmos_net_read_be32(p+i);out->has_router=1;}
  else if(tag==6&&len>=4){out->dns_server=cosmos_net_read_be32(p+i);out->has_dns=1;}
  else if(tag==54&&len==4){out->server_ip=cosmos_net_read_be32(p+i);out->has_server=1;}
  else if(tag==51&&len==4){out->lease_seconds=cosmos_net_read_be32(p+i);out->has_lease=1;}
  i+=len;
 }
 return out->message_type!=0;
}

/* DNS response helper for the simple A-record case. It skips compressed
   names safely and returns the first IPv4 answer matching IN/A. */
static int cosmos_dns_skip_name(const uint8_t *p,size_t n,size_t *offset) {
 size_t i=*offset;int jumped=0;size_t resume=0,steps=0;
 while(i<n&&steps++<128){
  uint8_t c=p[i++];
  if(c==0){if(!jumped)*offset=i;else *offset=resume;return 1;}
  if((c&0xC0u)==0xC0u){
   if(i>=n)return 0;
   size_t target=((size_t)(c&0x3Fu)<<8)|p[i++];
   if(target>=n)return 0;
   if(!jumped){resume=i;jumped=1;}
   i=target;continue;
  }
  if(c&0xC0u||c>63||i+c>n)return 0;
  i+=c;
 }
 return 0;
}
static int cosmos_dns_parse_a_response(const uint8_t *p,size_t n,
 uint16_t expected_id,uint32_t *address) {
 if(!p||!address||n<12||cosmos_net_read_be16(p)!=expected_id||
    !(p[2]&0x80u)||((p[2]&0x0Fu)!=0))return 0;
 uint16_t qd=cosmos_net_read_be16(p+4),an=cosmos_net_read_be16(p+6);
 if(!qd||!an||qd>16||an>64)return 0;
 size_t i=12;
 for(uint16_t q=0;q<qd;q++){
  if(!cosmos_dns_skip_name(p,n,&i)||i+4>n)return 0;
  i+=4;
 }
 for(uint16_t a=0;a<an;a++){
  if(!cosmos_dns_skip_name(p,n,&i)||i+10>n)return 0;
  uint16_t type=cosmos_net_read_be16(p+i),klass=cosmos_net_read_be16(p+i+2);
  uint16_t rdlen=cosmos_net_read_be16(p+i+8);i+=10;
  if(rdlen>n-i)return 0;
  if(type==1&&klass==1&&rdlen==4){*address=cosmos_net_read_be32(p+i);return 1;}
  i+=rdlen;
 }
 return 0;
}
#endif
