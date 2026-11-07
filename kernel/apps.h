#ifndef COSMOS_APPS_H
#define COSMOS_APPS_H
#include <stdint.h>
#include <stddef.h>

/* CosmOS application and package-management core.
   This is a kernel-side registry/validation layer, not a user-mode loader.
   Packages are not installed to disk until a writable VFS and executable
   runtime are available. */

#define COSMOS_APP_MAX 16
#define COSMOS_APP_ID_MAX 31
#define COSMOS_PACKAGE_MAGIC 0x50504143u /* "CAPP" little-endian */
#define COSMOS_PACKAGE_VERSION 1u

typedef enum {
 COSMOS_APP_BUILTIN=1,
 COSMOS_APP_PACKAGE=2,
 COSMOS_APP_SERVICE=3
} cosmos_app_kind;

typedef enum {
 COSMOS_APP_AVAILABLE=0,
 COSMOS_APP_INSTALLED=1,
 COSMOS_APP_RUNNING=2,
 COSMOS_APP_BLOCKED=3
} cosmos_app_status;

typedef struct {
 char id[COSMOS_APP_ID_MAX+1];
 char name[40];
 cosmos_app_kind kind;
 cosmos_app_status status;
 uint32_t version;
 uint32_t capabilities;
} cosmos_app_record;

typedef struct {
 uint32_t magic;
 uint16_t version;
 uint16_t header_size;
 uint32_t manifest_size;
 uint32_t payload_size;
 uint32_t payload_checksum;
 char app_id[32];
} cosmos_package_header;

typedef struct {
 uint32_t requests,completed,rejected;
 int backend_connected;
 int last_intent;
} cosmos_agent_state;

enum {
 COSMOS_AGENT_INTENT_NONE=0,
 COSMOS_AGENT_INTENT_OPEN_FINDER,
 COSMOS_AGENT_INTENT_OPEN_BROWSER,
 COSMOS_AGENT_INTENT_OPEN_NOTES,
 COSMOS_AGENT_INTENT_OPEN_SETTINGS,
 COSMOS_AGENT_INTENT_OPEN_CALENDAR,
 COSMOS_AGENT_INTENT_LIGHT_MODE,
 COSMOS_AGENT_INTENT_DARK_MODE,
 COSMOS_AGENT_INTENT_HELP
};

static cosmos_app_record cosmos_apps[COSMOS_APP_MAX];
static size_t cosmos_app_count;
static cosmos_agent_state cosmos_agent;

static size_t cosmos_app_copy(char *dst,size_t cap,const char *src) {
 size_t n=0;if(!dst||!cap||!src)return 0;
 while(src[n]&&n+1<cap){dst[n]=src[n];n++;}
 dst[n]=0;return n;
}
static int cosmos_app_register(const char *id,const char *name,
 cosmos_app_kind kind,cosmos_app_status status,uint32_t version) {
 if(!id||!name||cosmos_app_count>=COSMOS_APP_MAX)return 0;
 cosmos_app_record *a=&cosmos_apps[cosmos_app_count++];
 cosmos_app_copy(a->id,sizeof(a->id),id);
 cosmos_app_copy(a->name,sizeof(a->name),name);
 a->kind=kind;a->status=status;a->version=version;a->capabilities=0;
 return 1;
}
static void cosmos_apps_init(void) {
 cosmos_app_count=0;
 for(size_t i=0;i<sizeof(cosmos_agent);i++)((uint8_t*)&cosmos_agent)[i]=0;
 cosmos_app_register("com.cosmos.finder","Finder",COSMOS_APP_BUILTIN,COSMOS_APP_INSTALLED,1);
 cosmos_app_register("com.cosmos.cosmostar","CosmoStar",COSMOS_APP_BUILTIN,COSMOS_APP_BLOCKED,1);
 cosmos_app_register("com.cosmos.notes","Notes",COSMOS_APP_BUILTIN,COSMOS_APP_INSTALLED,1);
 cosmos_app_register("com.cosmos.settings","Settings",COSMOS_APP_BUILTIN,COSMOS_APP_INSTALLED,1);
 cosmos_app_register("com.cosmos.calendar","Calendar",COSMOS_APP_BUILTIN,COSMOS_APP_INSTALLED,1);
 cosmos_app_register("com.cosmos.agent","Cosmo Agent",COSMOS_APP_SERVICE,COSMOS_APP_BLOCKED,1);
 cosmos_app_register("com.google.chrome","Google Chrome",COSMOS_APP_PACKAGE,COSMOS_APP_BLOCKED,0);
 cosmos_app_register("com.mojang.minecraft","Minecraft",COSMOS_APP_PACKAGE,COSMOS_APP_BLOCKED,0);
}
static const cosmos_app_record *cosmos_app_find(const char *id) {
 if(!id)return NULL;
 for(size_t i=0;i<cosmos_app_count;i++){
  size_t n=0;while(id[n]&&cosmos_apps[i].id[n]&&id[n]==cosmos_apps[i].id[n])n++;
  if(!id[n]&&!cosmos_apps[i].id[n])return &cosmos_apps[i];
 }
 return NULL;
}
static int cosmos_app_set_status(const char *id,cosmos_app_status status) {
 if(!id)return 0;
 for(size_t i=0;i<cosmos_app_count;i++){
  size_t n=0;while(id[n]&&cosmos_apps[i].id[n]&&id[n]==cosmos_apps[i].id[n])n++;
  if(!id[n]&&!cosmos_apps[i].id[n]){cosmos_apps[i].status=status;return 1;}
 }
 return 0;
}
static uint32_t cosmos_package_checksum(const uint8_t *p,size_t n) {
 uint32_t h=2166136261u;if(!p&&n)return 0;
 for(size_t i=0;i<n;i++){h^=p[i];h*=16777619u;}
 return h;
}
static int cosmos_package_validate(const cosmos_package_header *h,
 const uint8_t *payload,size_t available) {
 if(!h||h->magic!=COSMOS_PACKAGE_MAGIC||h->version!=COSMOS_PACKAGE_VERSION||
    h->header_size!=sizeof(cosmos_package_header)||h->manifest_size>65536u||
    h->payload_size>available||(!payload&&h->payload_size))return 0;
 if(cosmos_package_checksum(payload,h->payload_size)!=h->payload_checksum)return 0;
 int terminated=0;
 for(size_t i=0;i<sizeof(h->app_id);i++)if(h->app_id[i]==0){terminated=1;break;}
 return terminated&&h->app_id[0]!=0;
}

/* Safe local intent recognizer. It can route known UI commands, but it is
   deliberately not represented as an LLM or online AI service. */
static int cosmos_agent_match(const char *s,const char *word) {
 if(!s||!word)return 0;
 while(*s==' ')s++;
 while(*word){char a=*s++,b=*word++;if(a>='A'&&a<='Z')a=(char)(a+32);if(b>='A'&&b<='Z')b=(char)(b+32);if(a!=b)return 0;}
 return *s==0||*s==' ';
}
static int cosmos_agent_classify(const char *text) {
 int intent=COSMOS_AGENT_INTENT_NONE;
 if(cosmos_agent_match(text,"open finder"))intent=COSMOS_AGENT_INTENT_OPEN_FINDER;
 else if(cosmos_agent_match(text,"open browser"))intent=COSMOS_AGENT_INTENT_OPEN_BROWSER;
 else if(cosmos_agent_match(text,"open notes"))intent=COSMOS_AGENT_INTENT_OPEN_NOTES;
 else if(cosmos_agent_match(text,"open settings"))intent=COSMOS_AGENT_INTENT_OPEN_SETTINGS;
 else if(cosmos_agent_match(text,"open calendar"))intent=COSMOS_AGENT_INTENT_OPEN_CALENDAR;
 else if(cosmos_agent_match(text,"light mode"))intent=COSMOS_AGENT_INTENT_LIGHT_MODE;
 else if(cosmos_agent_match(text,"dark mode"))intent=COSMOS_AGENT_INTENT_DARK_MODE;
 else if(cosmos_agent_match(text,"help"))intent=COSMOS_AGENT_INTENT_HELP;
 cosmos_agent.requests++;cosmos_agent.last_intent=intent;
 if(intent)cosmos_agent.completed++;else cosmos_agent.rejected++;
 return intent;
}
#endif
