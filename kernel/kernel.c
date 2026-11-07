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
static void roundrect(int x,int y,int w,int h,uint32_t c){rect(x+5,y,w-10,h,c);rect(x,y+5,w,h-10,c);rect(x+2,y+2,3,3,c);rect(x+w-5,y+2,3,3,c);rect(x+2,y+h-5,3,3,c);rect(x+w-5,y+h-5,3,3,c);}
static const uint8_t font[36][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static void text(int x,int y,const char*s,uint32_t c,int scale){for(;*s;s++,x+=6*scale){char ch=*s;if(ch>='a'&&ch<='z')ch-=32;if(ch==' ' )continue;if(ch==':'){rect(x+scale,y+scale,scale,scale,c);rect(x+scale,y+4*scale,scale,scale,c);continue;}if(ch=='-'){rect(x,y+3*scale,4*scale,scale,c);continue;}int i=-1;if(ch>='A'&&ch<='Z')i=ch-'A';else if(ch>='0'&&ch<='9')i=26+ch-'0';if(i<0)continue;for(int r=0;r<7;r++)for(int k=0;k<5;k++)if(font[i][r]&(1<<(4-k)))rect(x+k*scale,y+r*scale,scale,scale,c);}}
static void background(void){for(uint64_t y=0;y<sh;y++)for(uint64_t x=0;x<sw;x++){uint8_t b=32+(uint8_t)(y*28/sh);pixel((int)x,(int)y,0xFF000000u|(12u<<16)|(13u<<8)|b);}for(int i=0;i<70;i++){uint64_t x=(i*7919u+37u)%sw,y=(i*3571u+19u)%sh;pixel((int)x,(int)y,0xFF7776B8);}}
static void icon(int x,int y,int kind){uint32_t a=0xFF9B8CFF;roundrect(x,y,38,34,0xFF25243D);if(kind==0){rect(x+7,y+8,24,17,0xFFB8A7FF);rect(x+7,y+8,24,4,0xFFE8E4FF);}else if(kind==1){roundrect(x+7,y+5,24,23,0xFF6D9FEF);rect(x+10,y+9,18,2,0xFFFFFFFF);}else if(kind==2){roundrect(x+7,y+6,24,22,0xFF4BBE9A);rect(x+11,y+11,16,2,0xFFFFFFFF);rect(x+11,y+16,12,2,0xFFFFFFFF);}else if(kind==3){roundrect(x+7,y+6,24,22,0xFFDAA75D);rect(x+11,y+10,16,2,0xFFFFFFFF);rect(x+11,y+15,16,2,0xFFFFFFFF);}else{roundrect(x+7,y+6,24,22,a);rect(x+11,y+10,16,3,0xFFFFFFFF);}}
static int win=0, mouse_x=100,mouse_y=100, left_down=0;
static const char* names[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};
static void draw_window(void){if(!win)return;int w=(int)(sw*2/3),h=(int)(sh*2/3),x=((int)sw-w)/2,y=((int)sh-h)/2;roundrect(x,y,w,h,0xF21B1B2B);rect(x+1,y+1,w-2,30,0xFF302E49);for(int i=0;i<3;i++)roundrect(x+12+i*17,y+10,9,9,i==0?0xFFFF6B75:(i==1?0xFFFFCA65:0xFF67D99B));text(x+70,y+11,names[win-1],0xFFFFFFFF,1);rect(x+12,y+45,w-24,1,0xFF48465F);if(win==1){text(x+24,y+64,"COSMOS FILES",0xFFE4E1FF,2);text(x+24,y+100,"HOME",0xFFFFFFFF,1);text(x+24,y+125,"DESKTOP",0xFFBEB9DD,1);text(x+24,y+150,"DOCUMENTS",0xFFBEB9DD,1);}else if(win==2){text(x+24,y+65,"COSMOSTAR",0xFFE4E1FF,2);text(x+24,y+105,"BROWSER ENGINE NOT YET INCLUDED",0xFFFFD18A,1);text(x+24,y+130,"NETWORKING AND WEB RENDERING ARE NEXT",0xFFBEB9DD,1);}else if(win==3){text(x+24,y+65,"NOTES",0xFFE4E1FF,2);text(x+24,y+105,"NOTES SERVICE STARTER",0xFFBEB9DD,1);}else if(win==4){text(x+24,y+65,"SETTINGS",0xFFE4E1FF,2);text(x+24,y+105,"COSMOS SYSTEM SETTINGS",0xFFBEB9DD,1);}else if(win==5){text(x+24,y+65,"CALENDAR",0xFFE4E1FF,2);}else{text(x+24,y+65,"APP STORE",0xFFE4E1FF,2);text(x+24,y+105,"FIREFOX  MINECRAFT  FORTNITE",0xFFBEB9DD,1);}}
static void read_clock(char*out){uint8_t sec,min,hour;outb(0x70,0x00);sec=inb(0x71);outb(0x70,0x02);min=inb(0x71);outb(0x70,0x04);hour=inb(0x71);outb(0x70,0x0B);uint8_t mode=inb(0x71);int pm=(hour&0x80)!=0;hour&=0x7F;if(!(mode&4)){sec=(sec&15)+((sec>>4)*10);min=(min&15)+((min>>4)*10);if(!(mode&2))hour=(hour&15)+((hour>>4)*10);}if(!(mode&2)){hour%=12;if(pm)hour+=12;}int h=hour%12;if(!h)h=12;out[0]='0'+h/10;out[1]='0'+h%10;out[2]=':';out[3]='0'+min/10;out[4]='0'+min%10;out[5]=' ';out[6]=hour>=12?'P':'A';out[7]='M';out[8]=0;}
static void draw_desktop(void){background();rect(0,0,(int)sw,30,0xE91A192B);text(14,10,"COSMOS",0xFFFFFFFF,1);text(90,10,"DESKTOP",0xFFBEB9DD,1);char t[9];read_clock(t);text((int)sw-90,10,t,0xFFFFFFFF,1);
int labels_y=95;const char* labs[6]={"FINDER","COSMOSTAR","NOTES","SETTINGS","CALENDAR","APP STORE"};for(int i=0;i<6;i++){int x=28+(i%2)*112,y=58+(i/2)*90;icon(x,y,i);text(x-2,y+39,labs[i],0xFFE6E2FF,1);}
int dockw=430;if(dockw>(int)sw-20)dockw=(int)sw-20;int dx=((int)sw-dockw)/2,dy=(int)sh-62;roundrect(dx,dy,dockw,52,0xD92B2941);for(int i=0;i<6;i++)icon(dx+10+i*68,dy+8,i);(void)labels_y;draw_window();}
static const char *cursor[16]={"X...............","XX..............","XWX.............","XWWX............","XWWWX...........","XWWWWX..........","XWWWWWX.........","XWWWWWWX........","XWWWWWWWX.......","XWWWWXXXXX......","XWXWWX..........","XX.XWWX.........","X...XWWX........",".....XWWX.......","......XWWX......",".......XX......."};
static void cursor_draw(void){for(int y=0;y<16;y++)for(int x=0;x<16;x++){char c=cursor[y][x];if(c=='X')pixel(mouse_x+x,mouse_y+y,0xFF05050A);else if(c=='W')pixel(mouse_x+x,mouse_y+y,0xFFFFFFFF);}}
static int wait_empty(void){for(int i=0;i<100000;i++){if(!(inb(0x64)&2))return 1;pause_cpu();}return 0;}
static int read_aux(uint8_t*v){for(int i=0;i<100000;i++){uint8_t s=inb(0x64);if((s&0x21)==0x21){*v=inb(0x60);return 1;}pause_cpu();}return 0;}
static int mouse_cmd(uint8_t c){uint8_t r;if(!wait_empty())return 0;outb(0x64,0xD4);if(!wait_empty())return 0;outb(0x60,c);return read_aux(&r)&&r==0xFA;}
static int mouse_init(void){if(!wait_empty())return 0;outb(0x64,0xA8);if(!wait_empty())return 0;outb(0x64,0x20);uint8_t cfg=0;for(int i=0;i<100000;i++){if(inb(0x64)&1){cfg=inb(0x60);break;}pause_cpu();}cfg&=(uint8_t)~0x22;if(!wait_empty())return 0;outb(0x64,0x60);if(!wait_empty())return 0;outb(0x60,cfg);return mouse_cmd(0xF6)&&mouse_cmd(0xF4);}
static void handle_click(int x,int y){int dockw=430;if(dockw>(int)sw-20)dockw=(int)sw-20;int dx=((int)sw-dockw)/2,dy=(int)sh-62;if(y>=dy&&y<dy+52&&x>=dx&&x<dx+dockw){int n=(x-dx-10)/68;if(n>=0&&n<6)win=n+1;return;}if(win){int w=(int)(sw*2/3),h=(int)(sh*2/3),wx=((int)sw-w)/2,wy=((int)sh-h)/2;if(x>=wx+8&&x<wx+32&&y>=wy+5&&y<wy+25)win=0;}}
static void event_loop(void){uint8_t p[3];int n=0;for(;;){uint8_t s=inb(0x64);if(!(s&1)){pause_cpu();continue;}uint8_t d=inb(0x60);if(!(s&0x20))continue;if(n==0&&!(d&8))continue;p[n++]=d;if(n<3)continue;n=0;if(p[0]&0xC0)continue;mouse_x+=(int8_t)p[1];mouse_y-=(int8_t)p[2];if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;if(mouse_x>(int)sw-16)mouse_x=(int)sw-16;if(mouse_y>(int)sh-16)mouse_y=(int)sh-16;int pressed=(p[0]&1)!=0;if(pressed&&!left_down)handle_click(mouse_x,mouse_y);left_down=pressed;draw_desktop();cursor_draw();}}
void _start(void){struct limine_framebuffer_response*r=framebuffer_request.response;if(!r||!r->framebuffer_count)for(;;)__asm__ volatile("cli;hlt");struct limine_framebuffer*f=r->framebuffers[0];if(!f||!f->address||f->bpp!=32)for(;;)__asm__ volatile("cli;hlt");fb=f->address;sw=f->width;sh=f->height;stride=f->pitch/4;mouse_x=(int)sw/2;mouse_y=(int)sh/2;draw_desktop();cursor_draw();if(!mouse_init())for(;;)pause_cpu();event_loop();}
