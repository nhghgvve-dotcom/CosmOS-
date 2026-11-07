#ifndef COSMOS_STACK_H
#define COSMOS_STACK_H
#include <stdint.h>
#include <stddef.h>
#include "transport.h"
#include "e1000.h"

/* First live network service: DHCPv4 over the polled E1000 device.
   This is an early, single-interface stack stage, not a general socket API. */
#define COSMOS_DHCP_XID 0x434F534Du
#define COSMOS_DHCP_DISCOVER 1u
#define COSMOS_DHCP_OFFER 2u
#define COSMOS_DHCP_REQUEST 3u
#define COSMOS_DHCP_ACK 5u
#define COSMOS_DHCP_NAK 6u

static uint8_t cosmos_dhcp_state=0;
static uint32_t cosmos_dhcp_server=0;
static uint32_t cosmos_dhcp_offered=0;
static uint32_t cosmos_dhcp_xid=COSMOS_DHCP_XID;
static uint32_t cosmos_net_ticks=0;

static size_t cosmos_net_build_udp4_broadcast(uint8_t *frame,size_t cap,
 const uint8_t *payload,size_t payload_len,uint16_t source_port,uint16_t dest_port){
 if(!frame||!payload||cap<14+20+8+payload_len||payload_len>1400)return 0;
 for(size_t i=0;i<6;i++)frame[i]=0xFF;
 for(size_t i=0;i<6;i++)frame[6+i]=cosmos_e1000.mac[i];
 cosmos_net_write_be16(frame+12,COSMOS_ETH_TYPE_IPV4);
 uint8_t *ip=frame+14,*udp=ip+20;
 for(size_t i=0;i<20+8;i++)frame[14+i]=0;
 ip[0]=0x45;cosmos_net_write_be16(ip+2,(uint16_t)(20+8+payload_len));
 ip[8]=64;ip[9]=COSMOS_IPPROTO_UDP;
 cosmos_net_write_be32(ip+12,0);cosmos_net_write_be32(ip+16,0xFFFFFFFFu);
 cosmos_net_write_be16(ip+10,cosmos_net_checksum(ip,20));
 cosmos_net_write_be16(udp,source_port);cosmos_net_write_be16(udp+2,dest_port);
 cosmos_net_write_be16(udp+4,(uint16_t)(8+payload_len));cosmos_net_write_be16(udp+6,0);
 for(size_t i=0;i<payload_len;i++)udp[8+i]=payload[i];
 return 14+20+8+payload_len;
}
static int cosmos_dhcp_send_message(uint8_t type){
 uint8_t dhcp[300],frame[14+20+8+300];
 for(size_t i=0;i<sizeof(dhcp);i++)dhcp[i]=0;
 dhcp[0]=1;dhcp[1]=1;dhcp[2]=6;
 cosmos_net_write_be32(dhcp+4,cosmos_dhcp_xid);
 dhcp[10]=0x80;
 for(size_t i=0;i<6;i++)dhcp[28+i]=cosmos_e1000.mac[i];
 cosmos_net_write_be32(dhcp+236,0x63825363u);
 size_t n=240;
 dhcp[n++]=53;dhcp[n++]=1;dhcp[n++]=type;
 if(type==COSMOS_DHCP_REQUEST){
  dhcp[n++]=50;dhcp[n++]=4;cosmos_net_write_be32(dhcp+n,cosmos_dhcp_offered);n+=4;
  dhcp[n++]=54;dhcp[n++]=4;cosmos_net_write_be32(dhcp+n,cosmos_dhcp_server);n+=4;
 }
 dhcp[n++]=55;dhcp[n++]=4;dhcp[n++]=1;dhcp[n++]=3;dhcp[n++]=6;dhcp[n++]=15;
 dhcp[n++]=255;
 size_t fl=cosmos_net_build_udp4_broadcast(frame,sizeof(frame),dhcp,n,68,67);
 return fl&&cosmos_e1000_send(frame,fl);
}
static void cosmos_net_dispatch_frame(const uint8_t *frame,size_t length){
 const uint8_t *eth;uint16_t etype;
 if(!cosmos_net_ethernet_view(frame,length,&eth,&etype))return;
 if(etype!=COSMOS_ETH_TYPE_IPV4)return;
 const uint8_t *ip_payload;size_t ip_len;uint8_t proto;uint32_t src,dst;
 if(!cosmos_net_ipv4_view(eth,length-14,&ip_payload,&ip_len,&proto,&src,&dst))return;
 if(proto!=COSMOS_IPPROTO_UDP)return;
 uint16_t sport,dport;const uint8_t *udp_payload;size_t udp_len;
 if(!cosmos_net_udp_view(ip_payload,ip_len,&sport,&dport,&udp_payload,&udp_len))return;
 if(dport!=68||sport!=67||udp_len<240)return;
 if(cosmos_net_read_be32(udp_payload+4)!=cosmos_dhcp_xid)return;
 for(size_t i=0;i<6;i++)if(udp_payload[28+i]!=cosmos_e1000.mac[i])return;
 cosmos_dhcp_reply reply;
 if(!cosmos_dhcp_parse_reply(udp_payload,udp_len,&reply))return;
 if(reply.message_type==COSMOS_DHCP_OFFER&&cosmos_dhcp_state==1){
  cosmos_dhcp_offered=reply.offered_ip;
  cosmos_dhcp_server=reply.has_server?reply.server_ip:src;
  if(cosmos_dhcp_send_message(COSMOS_DHCP_REQUEST))cosmos_dhcp_state=2;
 }else if(reply.message_type==COSMOS_DHCP_ACK&&cosmos_dhcp_state==2){
  cosmos_net.ipv4_address=reply.offered_ip;
  cosmos_net.netmask=reply.has_subnet?reply.subnet_mask:0;
  cosmos_net.gateway=reply.has_router?reply.router:0;
  cosmos_net.dns_server=reply.has_dns?reply.dns_server:0;
  cosmos_net.dhcp_bound=1;
  cosmos_dhcp_state=3;
 }else if(reply.message_type==COSMOS_DHCP_NAK){
  cosmos_net.ipv4_address=0;cosmos_net.dhcp_bound=0;
  cosmos_dhcp_state=0;
 }
}
static void cosmos_net_stack_init(void){
 cosmos_dhcp_state=0;cosmos_net_ticks=0;
 if(cosmos_e1000.initialized&&cosmos_e1000.link_up&&cosmos_dhcp_send_message(COSMOS_DHCP_DISCOVER))
  cosmos_dhcp_state=1;
}
static void cosmos_net_stack_poll(void){
 cosmos_net_ticks++;
 if(!cosmos_e1000.initialized)return;
 cosmos_net.link_up=cosmos_e1000.link_up;
 /* Retry discovery/request periodically; no wall-clock timer is assumed yet. */
 if(!cosmos_net.dhcp_bound&&cosmos_net_ticks>5000000u){
  cosmos_net_ticks=0;
  if(cosmos_dhcp_state==0){
   if(cosmos_dhcp_send_message(COSMOS_DHCP_DISCOVER))cosmos_dhcp_state=1;
  }else if(cosmos_dhcp_state==1){
   cosmos_dhcp_send_message(COSMOS_DHCP_DISCOVER);
  }else if(cosmos_dhcp_state==2){
   cosmos_dhcp_send_message(COSMOS_DHCP_REQUEST);
  }
 }
}
#endif
