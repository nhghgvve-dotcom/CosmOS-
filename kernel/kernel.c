#include <stdint.h>
#include <stddef.h>
#include "limine.h"

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
static const uint8_t font[36][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static void text(int x,int y,const char*s,uint32_t c,int scale){for(;*s;s++,x+=6*scale){char ch=*s;if(ch>='a'&&ch<='z')ch-=32;if(ch==' ' )continue;if(ch==':'){rect(x+scale,y+scale,scale,scale,c);rect(x+scale,y+4*scale,scale,scale,c);continue;}if(ch=='-'){rect(x,y+3*scale,4*scale,scale,c);continue;}int i=-1;if(ch>='A'&&ch<='Z')i=ch-'A';else if(ch>='0'&&ch<='9')i=26+ch-'0';if(i<0)continue;for(int r=0;r<7;r++)for(int k=0;k<5;k++)if(font[i][r]&(1<<(4-k)))aa_circle(x+k*scale+scale/2,y+r*scale+scale/2,scale*3/4,c);}}
static void background(void){for(uint64_t y=0;y<sh;y++)for(uint64_t x=0;x<sw;x++){uint8_t b=32+(uint8_t)(y*28/sh);pixel((int)x,(int)y,0xFF000000u|(12u<<16)|(13u<<8)|b);}for(int i=0;i<70;i++){uint64_t x=(i*7919u+37u)%sw,y=(i*3571u+19u)%sh;pixel((int)x,(int)y,0xFF7776B8);}}
static void icon(int x,int y,int kind){uint32_t a=0xFF9B8CFF;roundrect(x,y,38,34,0xFF25243D);if(kind==0){rect(x+7,y+8,24,17,0xFFB8A7FF);rect(x+7,y+8,24,4,0xFFE8E4FF);}else if(kind==1){roundrect(x+7,y+5,24,23,0xFF6D9FEF);rect(x+10,y+9,18,2,0xFFFFFFFF);}else if(kind==2){roundrect(x+7,y+6,24,22,0xFF4BBE9A);rect(x+11,y+11,16,2,0xFFFFFFFF);rect(x+11,y+16,12,2,0xFFFFFFFF);}else if(kind==3){roundrect(x+7,y+6,24,22,0xFFDAA75D);rect(x+11,y+10,16,2,0xFFFFFFFF);rect(x+11,y+15,16,2,0xFFFFFFFF);}else{roundrect(x+7,y+6,24,22,a);rect(x+11,y+10,16,3,0xFFFFFFFF);}}
static int win=0, mouse_x=100,mouse_y=100, left_down=0, mouse_enabled=0, keyboard_selection=0;

/* Dock hover tooltip: save and restore the small area under the label so
   pointer movement never forces a full-screen redraw. */
static uint32_t tip_saved[140*34];
static int tip_x=0, tip_y=0, tip_visible=0;
static void tip_erase(void){
 if(!tip_visible)return;
 for(int y=0;y<34;y++)for(int x=0;x<140;x++)pixel(tip_x+x,tip_y+y,tip_saved[y*140+x]);
 tip_visible=0;
}
static void tip_draw(void){
 int dockh=350;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 if(mouse_x<dx||mouse_x>=dx+48||mouse_y<dy||mouse_y>=dy+dockh)return;
 int n=(mouse_y-dy)/56;
 if(n<0||n>=6)return;
 static const char *labels[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};
 tip_x=mouse_x+54;if(tip_x+140>(int)sw)tip_x=(int)sw-140;
 tip_y=mouse_y+8;if(tip_y+34>(int)sh)tip_y=(int)sh-34;
 if(tip_x<0||tip_y<0)return;
 for(int y=0;y<34;y++)for(int x=0;x<140;x++)tip_saved[y*140+x]=fb[(uint64_t)(tip_y+y)*stride+(uint64_t)(tip_x+x)];
 roundrect(tip_x,tip_y,140,34,0xF22B293D);
 text(tip_x+10,tip_y+12,labels[n],0xFFFFFFFF,2);
 tip_visible=1;
}
static const char* names[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};
static void draw_window(void){
 if(!win)return;
 int w=(int)(sw*2/3),h=(int)(sh*2/3),x=((int)sw-w)/2,y=((int)sh-h)/2;
 roundrect(x+5,y+7,w,h,0x60101020);
 roundrect(x,y,w,h,0xF21B1B2B);
 roundrect(x+1,y+1,w-2,38,0xFF302E49);
 for(int i=0;i<3;i++)aa_circle(x+19+i*19,y+18,5,i==0?0xFFFF6B75:(i==1?0xFFFFCA65:0xFF67D99B));
 text(x+82,y+15,names[win-1],0xFFFFFFFF,1);
 rect(x+14,y+48,w-28,1,0xFF48465F);
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
  roundrect(x+15,y+61,w-30,30,0xFF29283E);
  text(x+27,y+73,"GOOGLE SEARCH  |  COSMOSTAR",0xFFBEB9DD,1);
  roundrect(x+15,y+105,w-30,h-122,0xFF242338);
  text(x+34,y+133,"COSMOSTAR",0xFFE4E1FF,3);
  text(x+34,y+177,"YOUR COSMOS BROWSER",0xFFBEB9DD,1);
  roundrect(x+34,y+205,w-68,34,0xFF35324F);
  text(x+48,y+218,"SEARCH THE WEB",0xFFBEB9DD,1);
  text(x+34,y+270,"GOOGLE     YOUTUBE     WIKIPEDIA",0xFFB7A8FF,1);
  text(x+34,y+302,"Web rendering and networking are still being built.",0xFFFFD18A,1);
 }else if(win==3){
  roundrect(x+15,y+62,125,h-80,0xFF242338);
  text(x+29,y+80,"NOTES",0xFFE4E1FF,2);
  text(x+29,y+110,"ALL NOTES",0xFFBEB9DD,1);
  text(x+160,y+82,"WELCOME TO NOTES",0xFFE4E1FF,2);
  text(x+160,y+115,"The native notes service is a work in progress.",0xFFBEB9DD,1);
 }else if(win==4){
  text(x+24,y+68,"SYSTEM SETTINGS",0xFFE4E1FF,2);
  roundrect(x+20,y+96,w-40,48,0xFF302E49);
  text(x+36,y+116,"APPEARANCE",0xFFFFFFFF,1);
  roundrect(x+20,y+155,w-40,48,0xFF302E49);
  text(x+36,y+175,"DISPLAY",0xFFFFFFFF,1);
  roundrect(x+20,y+214,w-40,48,0xFF302E49);
  text(x+36,y+234,"SOUND",0xFFFFFFFF,1);
 }else if(win==5){
  text(x+24,y+68,"CALENDAR",0xFFE4E1FF,2);
  text(x+24,y+105,"YOUR SCHEDULE",0xFFBEB9DD,1);
 }else{
  text(x+24,y+68,"DISCOVER APPS",0xFFE4E1FF,2);
  roundrect(x+20,y+98,150,90,0xFF302E49);
  roundrect(x+184,y+98,150,90,0xFF302E49);
  roundrect(x+348,y+98,150,90,0xFF302E49);
  text(x+34,y+205,"FIREFOX",0xFFFFFFFF,1);
  text(x+198,y+205,"MINECRAFT",0xFFFFFFFF,1);
  text(x+362,y+205,"FORTNITE",0xFFFFFFFF,1);
  text(x+24,y+244,"App compatibility is being developed.",0xFFBEB9DD,1);
 }
}
static void read_clock(char*out){uint8_t sec,min,hour;outb(0x70,0x00);sec=inb(0x71);outb(0x70,0x02);min=inb(0x71);outb(0x70,0x04);hour=inb(0x71);outb(0x70,0x0B);uint8_t mode=inb(0x71);int pm=(hour&0x80)!=0;hour&=0x7F;if(!(mode&4)){sec=(sec&15)+((sec>>4)*10);min=(min&15)+((min>>4)*10);if(!(mode&2))hour=(hour&15)+((hour>>4)*10);}if(!(mode&2)){hour%=12;if(pm)hour+=12;}int h=hour%12;if(!h)h=12;out[0]='0'+h/10;out[1]='0'+h%10;out[2]=':';out[3]='0'+min/10;out[4]='0'+min%10;out[5]=' ';out[6]=hour>=12?'P':'A';out[7]='M';out[8]=0;}
static void draw_desktop(void){
 background();
 /* CosmOS purple space desktop: 25px glass menu bar and left glass dock. */
 rect(0,0,(int)sw,25,0xE9090914);
 text(12,9,"COSMOS",0xFFFFFFFF,1);
 text(76,9,"FILE",0xFFBEB9DD,1);
 text(108,9,"EDIT",0xFFBEB9DD,1);
 text(140,9,"VIEW",0xFFBEB9DD,1);
 text(176,9,"WINDOW",0xFFBEB9DD,1);
 text(226,9,"HELP",0xFFBEB9DD,1);
 char t[9];read_clock(t);text((int)sw-90,9,t,0xFFFFFFFF,1);
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
 int dockh=350;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 roundrect(dx,dy,48,dockh,0xA9191C2D);
 for(int i=0;i<6;i++)icon(dx+5,dy+9+i*56,i);
 draw_window();
}
static const char *cursor[16]={"X...............","XX..............","XWX.............","XWWX............","XWWWX...........","XWWWWX..........","XWWWWWX.........","XWWWWWWX........","XWWWWWWWX.......","XWWWWXXXXX......","XWXWWX..........","XX.XWWX.........","X...XWWX........",".....XWWX.......","......XWWX......",".......XX......."};
/* Save-under cursor: restore only the 16x16 region instead of repainting the
   entire framebuffer on every mouse packet. This prevents visible full-screen
   flashing and greatly reduces CPU work while moving the pointer. */
static uint32_t cursor_saved[16*16];
static int cursor_visible=0;
static void cursor_erase(void){if(!cursor_visible)return;for(int y=0;y<16;y++)for(int x=0;x<16;x++)pixel(mouse_x+x,mouse_y+y,cursor_saved[y*16+x]);cursor_visible=0;}
static void cursor_draw(void){for(int y=0;y<16;y++)for(int x=0;x<16;x++){cursor_saved[y*16+x]=fb[(uint64_t)(mouse_y+y)*stride+(uint64_t)(mouse_x+x)];char c=cursor[y][x];if(c=='X')pixel(mouse_x+x,mouse_y+y,0xFF05050A);else if(c=='W')pixel(mouse_x+x,mouse_y+y,0xFFFFFFFF);}cursor_visible=1;}
static int wait_empty(void){for(int i=0;i<100000;i++){if(!(inb(0x64)&2))return 1;pause_cpu();}return 0;}
static int read_aux(uint8_t*v){for(int i=0;i<100000;i++){uint8_t s=inb(0x64);if((s&0x21)==0x21){*v=inb(0x60);return 1;}pause_cpu();}return 0;}
static int mouse_cmd(uint8_t c){uint8_t r;if(!wait_empty())return 0;outb(0x64,0xD4);if(!wait_empty())return 0;outb(0x60,c);return read_aux(&r)&&r==0xFA;}
static int mouse_init(void){if(!wait_empty())return 0;outb(0x64,0xA8);if(!wait_empty())return 0;outb(0x64,0x20);uint8_t cfg=0;for(int i=0;i<100000;i++){if(inb(0x64)&1){cfg=inb(0x60);break;}pause_cpu();}cfg&=(uint8_t)~0x22;if(!wait_empty())return 0;outb(0x64,0x60);if(!wait_empty())return 0;outb(0x60,cfg);return mouse_cmd(0xF6)&&mouse_cmd(0xF4);}
static void handle_click(int x,int y){
 int dockh=350;if(dockh>(int)sh-45)dockh=(int)sh-45;
 int dx=8,dy=((int)sh-dockh)/2;
 if(x>=dx&&x<dx+48&&y>=dy&&y<dy+dockh){
  int n=(y-dy-9)/56;if(n>=0&&n<6)win=n+1;return;
 }
 if(win){
  int w=(int)(sw*2/3),h=(int)(sh*2/3),wx=((int)sw-w)/2,wy=((int)sh-h)/2;
  if(x>=wx+8&&x<wx+32&&y>=wy+5&&y<wy+25)win=0;
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
  if(pressed&&!left_down)handle_click(mouse_x,mouse_y);
  left_down=pressed;
  /* Only repaint the desktop when a click changes the active window. Normal
     pointer movement now touches just the old and new cursor rectangles. */
  if(win!=old_win)draw_desktop();
  tip_draw();
  cursor_draw();
 }
}
void _start(void){struct limine_framebuffer_response*r=framebuffer_request.response;if(!r||!r->framebuffer_count)for(;;)__asm__ volatile("cli;hlt");struct limine_framebuffer*f=r->framebuffers[0];if(!f||!f->address||f->bpp!=32||f->width<320||f->height<200||f->pitch<f->width*4)for(;;)__asm__ volatile("cli;hlt");fb=f->address;sw=f->width;sh=f->height;stride=f->pitch/4;mouse_x=(int)sw/2;mouse_y=(int)sh/2;draw_desktop();cursor_draw();mouse_enabled=mouse_init();event_loop();}
