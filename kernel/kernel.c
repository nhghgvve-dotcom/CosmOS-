#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "storage.h"
#include "pci.h"
#include "e1000.h"

/* CosmOS kernel framebuffer + first native desktop shell.
   This is freestanding kernel code: no hosted browser/HTML runtime yet. */
static volatile uint64_t limine_base_revision[3] __attribute__((used, section(".limine_requests"))) = LIMINE_BASE_REVISION(3);
static volatile uint64_t limine_requests_start_marker[4] __attribute__((used, section(".limine_requests_start"))) = LIMINE_REQUESTS_START_MARKER;
static volatile uint64_t limine_requests_end_marker[2] __attribute__((used, section(".limine_requests_end"))) = LIMINE_REQUESTS_END_MARKER;
static volatile struct limine_framebuffer_request framebuffer_request __attribute__((used, section(".limine_requests"))) = {
 .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = NULL
};

static uint32_t *fb;
static uint64_t sw, sh, stride;
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void pause_cpu(void){__asm__ volatile("pause");}
static void pixel(int x,int y,uint32_t c){if(x>=0&&y>=0&&(uint64_t)x<sw&&(uint64_t)y<sh)fb[(uint64_t)y*stride+(uint64_t)x]=c;}
static void rect(int x,int y,int w,int h,uint32_t c){if(w<=0||h<=0)return;for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)pixel(xx,yy,c);}
static uint32_t blend(uint32_t dst,uint32_t src,unsigned a){uint64_t ia=255-a;uint64_t rb=((((uint64_t)(dst&0x00FF00FFu))*ia+((uint64_t)(src&0x00FF00FFu))*a)>>8)&0x00FF00FFu;uint64_t g=((((uint64_t)(dst&0x0000FF00u))*ia+((uint64_t)(src&0x0000FF00u))*a)>>8)&0x0000FF00u;return 0xFF000000u|(uint32_t)rb|(uint32_t)g;}
static void coverage_pixel(int x,int y,uint32_t color,unsigned coverage){if(coverage>255)coverage=255;if(!coverage||x<0||y<0||(uint64_t)x>=sw||(uint64_t)y>=sh)return;uint64_t i=(uint64_t)y*stride+(uint64_t)x;fb[i]=blend(fb[i],color,coverage);}
static void aa_circle(int cx,int cy,int radius,uint32_t color){int x0=cx-radius-1,x1=cx+radius+1,y0=cy-radius-1,y1=cy+radius+1;for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){unsigned hit=0;for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){int dx=x*4+sx*1+0-cx*4-2,dy=y*4+sy*1+0-cy*4-2;if(dx*dx+dy*dy<=radius*radius*16)hit++;}coverage_pixel(x,y,color,hit*255/16);}}
static void roundrect(int x,int y,int w,int h,uint32_t c){if(w<=0||h<=0)return;int r=6;if(w<2*r)r=w/2;if(h<2*r)r=h/2;for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++){unsigned hit=0;for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){int px=xx*4+sx,py=yy*4+sy;int lx=(x+r)*4,hx=(x+w-r)*4,ly=(y+r)*4,hy=(y+h-r)*4;int qx=px<lx?lx:(px>hx?hx:px),qy=py<ly?ly:(py>hy?hy:py);int dx=px-qx,dy=py-qy;if(dx*dx+dy*dy<=r*r*16)hit++;}if(hit)coverage_pixel(xx,yy,c,hit*255/16);}}
#include "font.h"
static void text(int x,int y,const char*s,uint32_t c,int scale){
 int fi=scale<=1?0:(scale==2?1:2);
 for(;*s;s++){
  unsigned ch=(unsigned char)*s;
  if(ch<32||ch>126)ch='?';
  const uint8_t (*glyph)[32]=cosmos_font[fi][ch-32];
  for(int row=0;row<32;row++)for(int col=0;col<32;col++){
   unsigned a=glyph[row][col];
   if(a)coverage_pixel(x+col,y+row,c,a);
  }
  x+=cosmos_advance[fi][ch-32];
 }
}
static int light_mode=0;
static int settings_page=0;
/* Keep retired installer code buildable, but never launch it on startup. */
static int installer_open=0;
static int installer_step=0;
static void background(void){
 if(light_mode){
  /* Light theme: soft dawn sky, warm sun, and layered hills. */
  for(uint64_t y=0;y<sh;y++){
   uint32_t r=248-(uint32_t)(y*25/sh),g=246-(uint32_t)(y*12/sh),b=255-(uint32_t)(y*2/sh);
   uint32_t c=0xFF000000u|(r<<16)|(g<<8)|b;
   for(uint64_t x=0;x<sw;x++)pixel((int)x,(int)y,c);
  }
  aa_circle((int)(sw*3/4),(int)(sh/3),(int)(sh/11),0xFFFFD78E);
  aa_circle((int)(sw/4),(int)(sh*9/10),(int)(sh*3/5),0xFFB9C9E9);
  aa_circle((int)(sw*3/4),(int)(sh*19/20),(int)(sh*2/3),0xFFA8C8C1);
  rect(0,(int)(sh*4/5),(int)sw,(int)(sh/5),0xFF9BBDB4);
 }else{
  for(uint64_t y=0;y<sh;y++)for(uint64_t x=0;x<sw;x++){
   uint8_t b=32+(uint8_t)(y*28/sh);
   pixel((int)x,(int)y,0xFF000000u|(12u<<16)|(13u<<8)|b);
  }
  for(int i=0;i<70;i++){uint64_t x=(i*7919u+37u)%sw,y=(i*3571u+19u)%sh;pixel((int)x,(int)y,0xFF7776B8);}
 }
}
static void icon(int x,int y,int kind){
 roundrect(x,y,38,34,light_mode?0xFFE3E5F0:0xFF25243D);
 if(kind==0){ /* Finder: blue folder */
  roundrect(x+6,y+9,26,19,0xFF6DA9F5);roundrect(x+8,y+6,12,7,0xFF8CC2FF);rect(x+9,y+15,20,2,0xFFDCEBFF);
 }else if(kind==1){ /* CosmoStar: orbiting planet */
  aa_circle(x+19,y+17,12,0xFF8B79E8);aa_circle(x+19,y+17,7,light_mode?0xFFE3E5F0:0xFF25243D);rect(x+7,y+16,24,2,0xFFD8D0FF);aa_circle(x+28,y+10,3,0xFFFFFFFF);
 }else if(kind==2){ /* Notes: paper and lines */
  roundrect(x+9,y+4,21,26,0xFFF2EEDC);rect(x+13,y+11,13,2,0xFF67B99C);rect(x+13,y+16,13,2,0xFF67B99C);rect(x+13,y+21,10,2,0xFF67B99C);
 }else if(kind==3){ /* Settings: gear */
  aa_circle(x+19,y+17,12,0xFFDAA75D);aa_circle(x+19,y+17,5,light_mode?0xFFE3E5F0:0xFF25243D);
  rect(x+17,y+3,4,7,0xFFDAA75D);rect(x+17,y+24,4,7,0xFFDAA75D);rect(x+4,y+15,7,4,0xFFDAA75D);rect(x+27,y+15,7,4,0xFFDAA75D);
 }else if(kind==4){ /* Calendar */
  roundrect(x+7,y+5,24,25,0xFFF2F2F7);roundrect(x+7,y+5,24,9,0xFFE86D78);rect(x+12,y+17,4,3,0xFF72758B);rect(x+20,y+17,4,3,0xFF72758B);rect(x+12,y+23,4,3,0xFF72758B);rect(x+20,y+23,4,3,0xFF72758B);
 }else{ /* App Store: stylized A */
  aa_circle(x+19,y+17,14,0xFF6F67C9);rect(x+12,y+9,3,19,0xFFFFFFFF);rect(x+23,y+9,3,19,0xFFFFFFFF);rect(x+11,y+19,17,3,0xFFFFFFFF);
 }
}
static int win=0, mouse_x=100,mouse_y=100, left_down=0, mouse_enabled=0, keyboard_selection=0;
static int vm_detected=0;
static int storage_count=0;
static int partition_count=0;
static int fat32_count=0;
static cosmos_fat32_volume fat32_volume;
static int detect_virtual_machine(void){
 uint32_t a=1,b=0,c=0,d=0;
 __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
 return (c & (1u<<31)) != 0;
}

/* Dock hover tooltip: save and restore the small area under the label so
   pointer movement never forces a full-screen redraw. */
static uint32_t tip_saved[116*24];
static int tip_x=0, tip_y=0, tip_visible=0;
static void tip_erase(void){
 if(!tip_visible)return;
 for(int y=0;y<24;y++)for(int x=0;x<116;x++)pixel(tip_x+x,tip_y+y,tip_saved[y*116+x]);
 tip_visible=0;
}
static void tip_draw(void){
 if(tip_visible)return;
 int dockh=6*74+18;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 if(mouse_x<dx+13||mouse_x>=dx+51||mouse_y<dy+9||mouse_y>=dy+9+6*74)return;
 int n=(mouse_y-dy-9)/74;
 if(n<0||n>=6)return;
 static const char *labels[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};
 tip_x=dx;tip_y=dy+49+n*74;
 if(tip_x<0||tip_y<0||tip_x+116>(int)sw||tip_y+24>(int)sh)return;
 for(int y=0;y<24;y++)for(int x=0;x<116;x++)tip_saved[y*116+x]=fb[(uint64_t)(tip_y+y)*stride+(uint64_t)(tip_x+x)];
 roundrect(tip_x+4,tip_y,108,22,light_mode?0xF2FFFFFF:0xF22B293D);
 int len=0;while(labels[n][len])len++;
 text(tip_x+(116-len*8)/2,tip_y+6,labels[n],light_mode?0xFF20243A:0xFFFFFFFF,1);
 tip_visible=1;
}
static const char* names[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};
static int window_x=-1,window_y=-1,window_dragging=0,window_drag_dx=0,window_drag_dy=0;
static void window_bounds(int *x,int *y,int *w,int *h){
 *w=(int)(sw*2/3);*h=(int)(sh*2/3);
 *x=window_x<0?((int)sw-*w)/2:window_x;
 *y=window_y<0?((int)sh-*h)/2:window_y;
}
static void draw_window(void){
 if(!win)return;
 int w,h,x,y;window_bounds(&x,&y,&w,&h);
 roundrect(x+5,y+7,w,h,light_mode?0x30304050:0x60101020);
 roundrect(x,y,w,h,light_mode?0xF2F7F8FC:0xF21B1B2B);
 roundrect(x+1,y+1,w-2,38,light_mode?0xFFE4E8F2:0xFF302E49);
 for(int i=0;i<3;i++)aa_circle(x+19+i*19,y+18,5,i==0?0xFFFF6B75:(i==1?0xFFFFCA65:0xFF67D99B));
 text(x+82,y+15,names[win-1],light_mode?0xFF20243A:0xFFFFFFFF,1);
 rect(x+14,y+48,w-28,1,light_mode?0xFFD0D5E2:0xFF48465F);
 if(win==1){
  roundrect(x+14,y+62,112,h-78,0xFF242338);
  text(x+25,y+78,"FAVORITES",0xFFAAA4CC,1);
  text(x+25,y+105,"HOME",0xFFFFFFFF,1);
  text(x+25,y+130,"DESKTOP",0xFFBEB9DD,1);
  text(x+25,y+155,"DOCUMENTS",0xFFBEB9DD,1);
  text(x+25,y+180,"DOWNLOADS",0xFFBEB9DD,1);
  text(x+146,y+78,"RECENT FILES",0xFFE4E1FF,2);
  roundrect(x+145,y+105,118,82,0xFF302E49);
  roundrect(x+276,y+105,118,82,0xFF302E49);
  roundrect(x+407,y+105,118,82,0xFF302E49);
  text(x+160,y+205,"No files yet",0xFFBEB9DD,1);
 }else if(win==2){
  roundrect(x+15,y+61,w-30,34,light_mode?0xFFE2E7F0:0xFF29283E);
  text(x+27,y+73,"COSMOSTAR  |  ADDRESS BAR",light_mode?0xFF30364A:0xFFBEB9DD,1);
  roundrect(x+15,y+105,w-30,h-122,light_mode?0xFFEFF2F8:0xFF242338);
  text(x+34,y+133,"COSMOSTAR",0xFF8B7CE0,3);
  text(x+34,y+177,"WEB BROWSER",light_mode?0xFF30364A:0xFFBEB9DD,1);
  roundrect(x+34,y+205,w-68,38,light_mode?0xFFFFFFFF:0xFF35324F);
  text(x+48,y+219,"https://www.google.com",light_mode?0xFF30364A:0xFFBEB9DD,1);
  text(x+34,y+270,"A real web engine is not installed yet.",light_mode?0xFF9A5B28:0xFFFFD18A,1);
  text(x+34,y+302,"Networking, HTTPS, and page rendering are still needed.",light_mode?0xFF626B80:0xFFBEB9DD,1);
 }else if(win==3){
  roundrect(x+15,y+62,125,h-80,0xFF242338);
  text(x+29,y+80,"NOTES",0xFFE4E1FF,2);
  text(x+29,y+110,"ALL NOTES",0xFFBEB9DD,1);
  text(x+160,y+82,"WELCOME TO NOTES",0xFFE4E1FF,2);
  text(x+160,y+115,"The native notes service is a work in progress.",0xFFBEB9DD,1);
 }else if(win==4){
  text(x+24,y+68,"SYSTEM SETTINGS",0xFFE4E1FF,2);
  roundrect(x+20,y+96,w-40,48,light_mode?0xFFE6EAF3:0xFF302E49);
  text(x+36,y+116,light_mode?"APPEARANCE: LIGHT":"APPEARANCE: DARK",light_mode?0xFF20243A:0xFFFFFFFF,1);
  roundrect(x+20,y+155,w-40,48,light_mode?0xFFE6EAF3:0xFF302E49);
  text(x+36,y+175,"LANGUAGE & REGION",light_mode?0xFF20243A:0xFFFFFFFF,1);
  roundrect(x+20,y+214,w-40,48,light_mode?0xFFE6EAF3:0xFF302E49);
  text(x+36,y+234,"TIME ZONE",light_mode?0xFF20243A:0xFFFFFFFF,1);
  roundrect(x+20,y+273,w-40,48,light_mode?0xFFE6EAF3:0xFF302E49);
  text(x+36,y+293,"KEYBOARD LAYOUT",light_mode?0xFF20243A:0xFFFFFFFF,1);
  text(x+20,y+350,"Options UI added; saving preferences needs persistent storage.",light_mode?0xFF626B80:0xFFBEB9DD,1);
 }else if(win==5){
  text(x+24,y+68,"CALENDAR",0xFFE4E1FF,2);
  text(x+24,y+105,"YOUR SCHEDULE",0xFFBEB9DD,1);
 }else{
  text(x+24,y+68,"DISCOVER REAL APPLICATIONS",light_mode?0xFF20243A:0xFFE4E1FF,2);
  const int rowh=43; const int sy=y+100; const uint32_t card=light_mode?0xFFE6EAF3:0xFF302E49; const uint32_t fg=light_mode?0xFF20243A:0xFFFFFFFF;
  roundrect(x+20,sy,w-40,rowh,card);text(x+34,sy+14,"GOOGLE CHROME",fg,1);text(x+w-205,sy+14,"NOT COMPATIBLE YET",light_mode?0xFF8A5B2C:0xFFFFD18A,1);
  roundrect(x+20,sy+53,w-40,rowh,card);text(x+34,sy+67,"MINECRAFT LAUNCHER",fg,1);text(x+w-205,sy+67,"NOT COMPATIBLE YET",light_mode?0xFF8A5B2C:0xFFFFD18A,1);
  roundrect(x+20,sy+106,w-40,rowh,card);text(x+34,sy+120,"MINECRAFT",fg,1);text(x+w-205,sy+120,"NOT COMPATIBLE YET",light_mode?0xFF8A5B2C:0xFFFFD18A,1);
  roundrect(x+20,sy+159,w-40,rowh,card);text(x+34,sy+173,"MINECRAFT EDUCATION",fg,1);text(x+w-205,sy+173,"NOT COMPATIBLE YET",light_mode?0xFF8A5B2C:0xFFFFD18A,1);
  text(x+24,sy+225,"These are real app listings, not installable packages yet.",light_mode?0xFF626B80:0xFFBEB9DD,1);
  text(x+24,sy+253,"CosmOS still needs storage, networking, and an app runtime.",light_mode?0xFF626B80:0xFFBEB9DD,1);
 }
}
static void read_clock(char*out){uint8_t sec,min,hour;outb(0x70,0x00);sec=inb(0x71);outb(0x70,0x02);min=inb(0x71);outb(0x70,0x04);hour=inb(0x71);outb(0x70,0x0B);uint8_t mode=inb(0x71);int pm=(hour&0x80)!=0;hour&=0x7F;if(!(mode&4)){sec=(sec&15)+((sec>>4)*10);min=(min&15)+((min>>4)*10);if(!(mode&2))hour=(hour&15)+((hour>>4)*10);}if(!(mode&2)){hour%=12;if(pm)hour+=12;}int h=hour%12;if(!h)h=12;out[0]='0'+h/10;out[1]='0'+h%10;out[2]=':';out[3]='0'+min/10;out[4]='0'+min%10;out[5]=' ';out[6]=hour>=12?'P':'A';out[7]='M';out[8]=0;}

static int installer_panel_x(void){return (int)sw/10;}
static int installer_panel_y(void){return (int)sh/10;}
static int installer_panel_w(void){return (int)sw*8/10;}
static int installer_panel_h(void){return (int)sh*8/10;}
static void installer_wallpaper(void){
 for(uint64_t y=0;y<sh;y++)for(uint64_t x=0;x<sw;x++){
  unsigned v=(unsigned)(y*24/sh);
  uint32_t c=0xFF000000u|((8u+v/3)<<16)|((12u+v/2)<<8)|(30u+v);
  int nx=(int)(x*1000/sw)-500, ny=(int)(y*1000/sh)-500;
  int d1=(nx+190)*(nx+190)+(ny-70)*(ny-70);
  int d2=(nx-260)*(nx-260)+(ny+150)*(ny+150);
  if(d1<180000)c=blend(c,0xFF30245E,(unsigned)((180000-d1)*48/180000));
  if(d2<220000)c=blend(c,0xFF173F60,(unsigned)((220000-d2)*34/220000));
  pixel((int)x,(int)y,c);
 }
 for(int i=0;i<95;i++){
  int x=(int)((i*7919u+37u)%sw),y=(int)((i*3571u+19u)%sh);
  pixel(x,y,(i%4)?0xFF777DAD:0xFFD5D8F4);
 }
 /* Soft horizon glow, part of CosmOS's own wallpaper. */
 for(int y=(int)sh*2/3;y<(int)sh*2/3+18;y+=3)
  rect(0,y,(int)sw,1,0x102B5A86);
}
static void draw_installer(void){
 installer_wallpaper();
 int px=installer_panel_x(),py=installer_panel_y(),pw=installer_panel_w(),ph=installer_panel_h();
 int side=pw/3;
 /* Dark, translucent installer window with macOS-inspired proportions. */
 roundrect(px+7,py+10,pw,ph,0x70000000);
 roundrect(px,py,pw,ph,0xF0161924);
 roundrect(px+1,py+1,pw-2,58,0xFF202431);
 rect(px+1,py+58,pw-2,1,0xFF343949);
 for(int i=0;i<3;i++)aa_circle(px+25+i*22,py+29,6,i==0?0xFFFF6B70:(i==1?0xFFFFC45E:0xFF65D58A));
 text(px+pw/2-130,py+16,"CosmOS Installer",0xFFF2F3FA,1);
 /* Sidebar and step progress. */
 roundrect(px+1,py+59,side-1,ph-60,0xFF191D29);
 static const char *steps[6]={"Welcome","Language and Region","Keyboard Layout","Installation Disk","Create Account","Install CosmOS"};
 for(int i=0;i<6;i++){
  int yy=py+112+i*76;
  uint32_t fg=i==installer_step?0xFFFFFFFF:0xFF9DA5B8;
  if(i==installer_step)roundrect(px+22,yy-10,side-44,56,0xFF30384B);
  aa_circle(px+48,yy+12,11,i<installer_step?0xFF83CDAA:(i==installer_step?0xFF9D8BFF:0xFF555D70));
  if(i<installer_step)text(px+43,yy+2,"+",0xFF17251F,1);
  text(px+72,yy+1,steps[i],fg,1);
 }
 text(px+30,py+ph-74,"COSMOS",0xFF9D8BFF,1);
 text(px+30,py+ph-42,"A new world awaits.",0xFF8F98AC,1);
 /* Main content area. */
 int tx=px+side+66,ty=py+112;
 static const char *titles[6]={"Welcome to CosmOS","Choose Your Language","Choose a Keyboard","Select an Installation Disk","Create Your Account","Ready to Install"};
 static const char *descs[6]={
  "A fresh, modern desktop built for your computer.",
  "Choose the language and region for your new system.",
  "Choose the keyboard layout you want to use.",
  "Choose where CosmOS should be installed.",
  "Set up the name for your new computer.",
  "Review your choices before continuing."
 };
 text(tx,ty,titles[installer_step],0xFFF4F5FB,2);
 text(tx,ty+52,descs[installer_step],0xFFB9C1D2,1);
 if(installer_step==0){
  aa_circle(tx+90,ty+190,52,0xFF9D8BFF);
  aa_circle(tx+90,ty+190,27,0xFFECE8FF);
  text(tx+175,ty+164,"Welcome to your new CosmOS.",0xFFE8EAF4,1);
  text(tx+175,ty+204,"Let's get everything ready.",0xFF9DA7BC,1);
 }else if(installer_step==1){
  roundrect(tx,ty+112,460,58,0xFF292F3E);text(tx+22,ty+128,"English (United States)",0xFFF0F2F8,1);
  roundrect(tx,ty+190,460,58,0xFF292F3E);text(tx+22,ty+206,"United States",0xFFF0F2F8,1);
 }else if(installer_step==2){
  roundrect(tx,ty+112,460,58,0xFF292F3E);text(tx+22,ty+128,"U.S. Keyboard  -  Selected",0xFFF0F2F8,1);
 }else if(installer_step==3){
  if(storage_count>0){
   roundrect(tx,ty+112, pw-side-130,76,0xFF263B39);
   text(tx+20,ty+128,"IDE VIRTUAL DISK DETECTED",0xFF9BE6C3,1);
   text(tx+20,ty+154,"A storage device is visible to CosmOS.",0xFFD5F6E7,1);
   text(tx,ty+214,"Disk formatting and system-file installation are not ready.",0xFFFFD18A,1);
  }else{
   roundrect(tx,ty+112,pw-side-130,76,0xFF382A30);
   text(tx+20,ty+128,vm_detected?"NO SUPPORTED VOLUME DETECTED":"NO IDE DISK DETECTED",0xFFFFC2A8,1);
   text(tx+20,ty+154,vm_detected?"No FAT32 volume was found.":"Connect an IDE virtual disk in VMware.",0xFFFFD8CC,1);
   text(tx,ty+214,"This preview cannot format a disk or install system files yet.",0xFFFFD18A,1);
  }
  if(vm_detected)text(tx,ty+260,"Virtual machine preview mode - no disk changes will be made.",0xFF9DA7BC,1);
 }else if(installer_step==4){
  roundrect(tx,ty+112,460,58,0xFF292F3E);text(tx+22,ty+128,"Computer name: CosmOS-PC",0xFFF0F2F8,1);
  roundrect(tx,ty+190,460,58,0xFF292F3E);text(tx+22,ty+206,"User account: not configured",0xFFF0F2F8,1);
  text(tx,ty+274,"Account saving will be enabled with filesystem support.",0xFFFFD18A,1);
 }else{
  text(tx,ty+112,"Language: English (United States)",0xFFD0D5E2,1);
  text(tx,ty+158,"Keyboard: U.S.",0xFFD0D5E2,1);
  text(tx,ty+204,storage_count>0?"Disk: IDE device detected (not formatted)":(vm_detected?"Disk: preview mode - no volume detected":"Disk: unavailable"),0xFFD0D5E2,1);
  text(tx,ty+274,vm_detected?"Preview only. No files will be installed or disk changed.":"Installation is not available until disk support is complete.",0xFFFFD18A,1);
  text(tx,ty+310,fat32_count?"FAT32 volume detected, but system-file copying is not connected.":"No FAT32 volume detected in this build.",0xFFFFD18A,1);
 }
 /* Footer controls remain inside the installer window. */
 int by=py+ph-76;
 roundrect(px+pw-340,by,126,48,0xFF303747);
 text(px+pw-312,by+12,"CANCEL",0xFFE7EAF2,1);
 roundrect(px+pw-198,by,166,48,installer_step==5?0xFF51466E:0xFF7867B5);
 text(px+pw-166,by+12,installer_step==5?(vm_detected?"CLOSE":"BLOCKED"):"CONTINUE",0xFFFFFFFF,1);
 text(px+24,py+ph-28,"DARK MODE  |  COSMOS SETUP",0xFF7F899D,1);
}

/* Cache the clean desktop so opening or moving a window does not
   repaint the wallpaper and every desktop decoration on each mouse packet. */
#define COSMOS_CACHE_WIDTH 2560
#define COSMOS_CACHE_HEIGHT 1440
static uint32_t desktop_cache[COSMOS_CACHE_WIDTH*COSMOS_CACHE_HEIGHT];
static int desktop_cache_valid=0,desktop_cache_light=-1;
/* Remember the last window rectangle so mouse movement restores only that
   rectangle from the cached desktop, rather than copying the whole screen. */
static int cached_window_visible=0;
static int cached_window_x=0,cached_window_y=0,cached_window_w=0,cached_window_h=0;
static void restore_cached_rect(int x,int y,int w,int h){
 int x0=x-7,y0=y-9,x1=x+w+7,y1=y+h+9;
 if(x0<0)x0=0;if(y0<0)y0=0;
 if(x1>(int)sw)x1=(int)sw;if(y1>(int)sh)y1=(int)sh;
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)
  fb[(uint64_t)yy*stride+(uint64_t)xx]=desktop_cache[(uint64_t)yy*sw+(uint64_t)xx];
}
static void draw_desktop_base(void){
 background();
 /* Futuristic CosmOS space desktop with a translucent vertical dock. */
 /* Modern translucent menu bar with original CosmOS branding. */
 rect(0,0,(int)sw,30,light_mode?0xEAF7F8FC:0xE6111228);
 rect(0,29,(int)sw,1,light_mode?0x40AAB2C4:0x403F3B68);
 aa_circle(19,15,7,0xFF9B8CFF);
 aa_circle(19,15,3,0xFFECE8FF);
 aa_circle(25,9,2,0xFFB8A7FF);
 text(34,10,"CosmOS",light_mode?0xFF20243A:0xFFFFFFFF,1);
 text(104,10,"File",light_mode?0xFF34394D:0xFFE0DDF5,1);
 text(138,10,"Edit",light_mode?0xFF34394D:0xFFE0DDF5,1);
 text(170,10,"View",light_mode?0xFF34394D:0xFFE0DDF5,1);
 text(210,10,"Window",light_mode?0xFF34394D:0xFFE0DDF5,1);
 text((int)sw/2-28,10,"COSMOS",light_mode?0xFF5D6680:0xFFE8E4FF,1);
 text(266,10,"Help",light_mode?0xFF34394D:0xFFE0DDF5,1);
 char t[9];read_clock(t);text((int)sw-90,10,t,light_mode?0xFF20243A:0xFFFFFFFF,1);
 /* Subtle purple nebula highlights. */
 for(int y=25;y<(int)sh;y+=4){
  int band=(int)((y*19u)%83u);
  if(band<8)rect(48,y,(int)sw-48,1,0x101B1240);
 }
 /* Notes.txt desktop shortcut at upper right. */
 int fx=(int)sw-82;
 icon(fx,42,3);
 text(fx-3,82,"NOTES",0xFFE6E2FF,1);
 /* Vertical translucent dock matching the supplied desktop. */
 int dockh=6*74+18;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 roundrect(dx,dy,64,dockh,light_mode?0xDDE9ECF5:0xB51A2140);
 for(int i=0;i<6;i++)icon(dx+13,dy+9+i*74,i);
}
static void draw_desktop(void){
 if(installer_open){draw_installer();return;}
 int cacheable=sw<=COSMOS_CACHE_WIDTH&&sh<=COSMOS_CACHE_HEIGHT;
 if(!cacheable){
  draw_desktop_base();
  draw_window();
  cached_window_visible=0;
  return;
 }
 if(!desktop_cache_valid||desktop_cache_light!=light_mode){
  draw_desktop_base();
  for(uint64_t y=0;y<sh;y++)for(uint64_t x=0;x<sw;x++)
   desktop_cache[y*sw+x]=fb[y*stride+x];
  desktop_cache_valid=1;
  desktop_cache_light=light_mode;
  cached_window_visible=0;
 }else if(cached_window_visible){
  restore_cached_rect(cached_window_x,cached_window_y,
                      cached_window_w,cached_window_h);
  cached_window_visible=0;
 }
 if(win){
  int x,y,w,h;window_bounds(&x,&y,&w,&h);
  draw_window();
  cached_window_x=x;cached_window_y=y;
  cached_window_w=w;cached_window_h=h;
  cached_window_visible=1;
 }
}
static const char *cursor[16]={"X...............","XX..............","XWX.............","XWWX............","XWWWX...........","XWWWWX..........","XWWWWWX.........","XWWWWWWX........","XWWWWWWWX.......","XWWWWXXXXX......","XWXWWX..........","XX.XWWX.........","X...XWWX........",".....XWWX.......","......XWWX......",".......XX......."};
/* Save-under cursor: restore only the 16x16 region instead of repainting the
   entire framebuffer on every mouse packet. This prevents visible full-screen
   flashing and greatly reduces CPU work while moving the pointer. */
static uint32_t cursor_saved[16*16];
static int cursor_visible=0;
static void cursor_erase(void){if(!cursor_visible)return;for(int y=0;y<16;y++)for(int x=0;x<16;x++){int px=mouse_x+x,py=mouse_y+y;if(px>=0&&py>=0&&(uint64_t)px<sw&&(uint64_t)py<sh)pixel(px,py,cursor_saved[y*16+x]);}cursor_visible=0;}
static void cursor_draw(void){for(int y=0;y<16;y++)for(int x=0;x<16;x++){int px=mouse_x+x,py=mouse_y+y;if(px<0||py<0||(uint64_t)px>=sw||(uint64_t)py>=sh){cursor_saved[y*16+x]=0;continue;}cursor_saved[y*16+x]=fb[(uint64_t)py*stride+(uint64_t)px];char c=cursor[y][x];if(c=='X')pixel(px,py,0xFF05050A);else if(c=='W')pixel(px,py,0xFFFFFFFF);}cursor_visible=1;}
static int wait_empty(void){for(int i=0;i<100000;i++){if(!(inb(0x64)&2))return 1;pause_cpu();}return 0;}
static int read_aux(uint8_t*v){for(int i=0;i<100000;i++){uint8_t s=inb(0x64);if((s&0x21)==0x21){*v=inb(0x60);return 1;}pause_cpu();}return 0;}
static int mouse_cmd(uint8_t c){uint8_t r;if(!wait_empty())return 0;outb(0x64,0xD4);if(!wait_empty())return 0;outb(0x60,c);return read_aux(&r)&&r==0xFA;}
static int mouse_init(void){if(!wait_empty())return 0;outb(0x64,0xA8);if(!wait_empty())return 0;outb(0x64,0x20);uint8_t cfg=0;for(int i=0;i<100000;i++){if(inb(0x64)&1){cfg=inb(0x60);break;}pause_cpu();}cfg&=(uint8_t)~0x22;if(!wait_empty())return 0;outb(0x64,0x60);if(!wait_empty())return 0;outb(0x60,cfg);return mouse_cmd(0xF6)&&mouse_cmd(0xF4);}
static void handle_click(int x,int y){
 int dockh=6*74+18;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 if(x>=dx+13&&x<dx+51&&y>=dy+9&&y<dy+9+6*74){
  int n=(y-dy-9)/74;if(n>=0&&n<6)win=n+1;return;
 }
 if(win){
   int w,h,wx,wy;window_bounds(&wx,&wy,&w,&h);
   if(x>=wx+8&&x<wx+32&&y>=wy+5&&y<wy+25){win=0;window_dragging=0;window_x=-1;window_y=-1;}
   else if(y>=wy&&y<wy+38&&x>=wx+42&&x<wx+w){
    window_dragging=1;window_drag_dx=x-wx;window_drag_dy=y-wy;
   }
  else if(win==4&&x>=wx+20&&x<wx+w-20&&y>=wy+96&&y<=wy+144){light_mode=!light_mode;}
  else if(win==4&&x>=wx+20&&x<wx+w-20&&y>=wy+155&&y<=wy+203){settings_page=1;}
  else if(win==4&&x>=wx+20&&x<wx+w-20&&y>=wy+214&&y<=wy+262){settings_page=2;}
  else if(win==4&&x>=wx+20&&x<wx+w-20&&y>=wy+273&&y<=wy+321){settings_page=3;}
 }
}
static void event_loop(void){
 uint8_t p[3];int n=0;
 for(;;){
  uint8_t status=inb(0x64);
  if(!(status&1)){pause_cpu();continue;}
  uint8_t data=inb(0x60);
  if(!(status&0x20)){
   /* Keyboard navigation works alongside the mouse, not only as a fallback. */
   if(data==0xE0)continue;
   if(data==0x4B){keyboard_selection=(keyboard_selection+5)%6;}
   else if(data==0x4D){keyboard_selection=(keyboard_selection+1)%6;}
   else if(data==0x1C){win=keyboard_selection+1;}
   else if(data==0x01){win=0;}
   else continue;
   cursor_erase();tip_erase();draw_desktop();tip_draw();cursor_draw();
   continue;
  }
  if(!mouse_enabled)continue;
  if(n==0&&!(data&8))continue;
  p[n++]=data;
  if(n<3)continue;
  n=0;
  if(p[0]&0xC0)continue;
  cursor_erase();
  tip_erase();
  mouse_x+=(int8_t)p[1];mouse_y-=(int8_t)p[2];
  if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;
  if(mouse_x>(int)sw-16)mouse_x=(int)sw-16;
  if(mouse_y>(int)sh-16)mouse_y=(int)sh-16;
  int pressed=(p[0]&1)!=0;
   int old_win=win;
   int was_dragging=window_dragging;
   if(pressed&&!left_down)handle_click(mouse_x,mouse_y);
   if(!pressed&&left_down)window_dragging=0;
   if(pressed&&window_dragging&&win){
    int ww,wh,wx,wy;window_bounds(&wx,&wy,&ww,&wh);
    window_x=mouse_x-window_drag_dx;window_y=mouse_y-window_drag_dy;
    if(window_x<0)window_x=0;
    if(window_y<30)window_y=30;
    if(window_x>(int)sw-ww)window_x=(int)sw-ww;
    if(window_y>(int)sh-40)window_y=(int)sh-40;
   }
   left_down=pressed;
   if(win!=old_win||(pressed&&window_dragging)||was_dragging)draw_desktop();
  tip_draw();
  cursor_draw();
 }
}
void _start(void){struct limine_framebuffer_response*r=framebuffer_request.response;if(!r||!r->framebuffer_count)for(;;)__asm__ volatile("cli;hlt");struct limine_framebuffer*f=r->framebuffers[0];if(!f||!f->address||f->bpp!=32||f->width<320||f->height<200||f->pitch<f->width*4)for(;;)__asm__ volatile("cli;hlt");fb=f->address;sw=f->width;sh=f->height;stride=f->pitch/4;vm_detected=detect_virtual_machine();cosmos_pci_init();cosmos_e1000_probe();storage_count=cosmos_storage_init();
 for(uint8_t d=0;d<4;d++) if(cosmos_ata_devices[d].present){
  cosmos_partition found[4]; int n=cosmos_partition_scan(d,found,4); partition_count+=n;
  for(int i=0;i<n;i++) if(found[i].type==0x0B||found[i].type==0x0C||found[i].type==0x0E||found[i].type==0x06)
   if(cosmos_fat32_mount(d,found[i].start_lba,&fat32_volume)) fat32_count++;
 }
 mouse_x=(int)sw/2;mouse_y=(int)sh/2;draw_desktop();cursor_draw();mouse_enabled=mouse_init();event_loop();}
