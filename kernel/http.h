#ifndef COSMOS_HTTP_H
#define COSMOS_HTTP_H
#include <stdint.h>
#include <stddef.h>

/* Bounded HTTP/1.x response parser for the future browser networking layer.
   It parses already-received plaintext bytes; it does not open sockets,
   perform TLS, or render HTML. */
#define COSMOS_HTTP_MAX_HEADERS 32
#define COSMOS_HTTP_LINE_MAX 1024

typedef struct {
 unsigned status_code;
 size_t header_bytes;
 const uint8_t *body;
 size_t body_length;
 size_t content_length;
 int has_content_length;
 int chunked;
 int connection_close;
 char content_type[96];
} cosmos_http_response;

static int cosmos_http_equal_ci(const char *a,const char *b,size_t n) {
 for(size_t i=0;i<n;i++){
  char x=a[i],y=b[i];
  if(x>='A'&&x<='Z')x=(char)(x+32);
  if(y>='A'&&y<='Z')y=(char)(y+32);
  if(x!=y)return 0;
 }
 return 1;
}
static size_t cosmos_http_find_crlf(const uint8_t *p,size_t n,size_t start) {
 for(size_t i=start;i+1<n;i++)if(p[i]=='\r'&&p[i+1]=='\n')return i;
 return n;
}
static int cosmos_http_parse_decimal(const char *p,size_t n,size_t *value) {
 if(!n)return 0;size_t v=0;
 for(size_t i=0;i<n;i++){if(p[i]<'0'||p[i]>'9')return 0;
  unsigned d=(unsigned)(p[i]-'0');if(v>((size_t)-1-d)/10)return 0;v=v*10+d;}
 *value=v;return 1;
}
static int cosmos_http_parse_response(const uint8_t *p,size_t n,cosmos_http_response *out) {
 if(!p||!out||n<12)return 0;
 for(size_t z=0;z<sizeof(*out);z++)((uint8_t*)out)[z]=0;
 size_t line=cosmos_http_find_crlf(p,n,0);if(line==n||line<12)return 0;
 if(!((line>=9&&!__builtin_memcmp(p,"HTTP/1.0 ",9))||
      (line>=9&&!__builtin_memcmp(p,"HTTP/1.1 ",9))))return 0;
 if(p[9]<'0'||p[9]>'9'||p[10]<'0'||p[10]>'9'||p[11]<'0'||p[11]>'9')return 0;
 out->status_code=(unsigned)(p[9]-'0')*100u+(unsigned)(p[10]-'0')*10u+(unsigned)(p[11]-'0');
 size_t pos=line+2;unsigned count=0;
 while(pos+1<n){
  size_t end=cosmos_http_find_crlf(p,n,pos);if(end==n)return 0;
  if(end==pos){pos=end+2;out->header_bytes=pos;out->body=p+pos;out->body_length=n-pos;return 1;}
  if(++count>COSMOS_HTTP_MAX_HEADERS)return 0;
  size_t colon=pos;while(colon<end&&p[colon]!=':')colon++;
  if(colon==pos||colon==end)return 0;
  size_t v=colon+1;while(v<end&&(p[v]==' '||p[v]=='\t'))v++;
  size_t ve=end;while(ve>v&&(p[ve-1]==' '||p[ve-1]=='\t'))ve--;
  size_t kn=colon-pos,vn=ve-v;
  if(kn==14&&cosmos_http_equal_ci((const char*)p+pos,"Content-Length",14)){
   size_t val;if(!cosmos_http_parse_decimal((const char*)p+v,vn,&val))return 0;
   out->content_length=val;out->has_content_length=1;
  }else if(kn==17&&cosmos_http_equal_ci((const char*)p+pos,"Transfer-Encoding",17)){
   if(vn>=7&&cosmos_http_equal_ci((const char*)p+v,"chunked",7))out->chunked=1;
  }else if(kn==10&&cosmos_http_equal_ci((const char*)p+pos,"Connection",10)){
   if(vn==5&&cosmos_http_equal_ci((const char*)p+v,"close",5))out->connection_close=1;
  }else if(kn==12&&cosmos_http_equal_ci((const char*)p+pos,"Content-Type",12)){
   size_t copy=vn<sizeof(out->content_type)-1?vn:sizeof(out->content_type)-1;
   for(size_t j=0;j<copy;j++)out->content_type[j]=(char)p[v+j];
   out->content_type[copy]=0;
  }
  pos=end+2;
 }
 return 0;
}

/* URL decomposition for http:// and https:// origins. Hostnames are kept
   as slices into the caller's URL; no DNS or normalization is performed. */
typedef struct {
 const char *host,*path;
 size_t host_length,path_length;
 uint16_t port;
 int secure;
} cosmos_url;
static int cosmos_url_parse(const char *s,size_t n,cosmos_url *out) {
 if(!s||!out)return 0;size_t start;uint16_t port;
 if(n>=7&&!__builtin_memcmp(s,"http://",7)){start=7;port=80;out->secure=0;}
 else if(n>=8&&!__builtin_memcmp(s,"https://",8)){start=8;port=443;out->secure=1;}
 else return 0;
 size_t end=start;while(end<n&&s[end]!='/'&&s[end]!='?'&&s[end]!='#'&&s[end]!=':')end++;
 if(end==start)return 0;
 size_t authority_end=end;
 if(end<n&&s[end]==':'){
  size_t p=end+1;size_t v=0;if(p==n)return 0;
  while(p<n&&s[p]>='0'&&s[p]<='9'){v=v*10u+(unsigned)(s[p]-'0');if(v>65535)return 0;p++;}
  if(p==end+1||v==0)return 0;port=(uint16_t)v;authority_end=p;end=p;
 }
 if(end<n&&s[end]!='/'&&s[end]!='?'&&s[end]!='#')return 0;
 out->host=s+start;out->host_length=(end==authority_end?authority_end:end)-start;
 /* Strip an explicit port from the host slice. */
 if(authority_end>start&&s[authority_end-1]>='0'&&s[authority_end-1]<='9'){
  size_t h=start;while(h<authority_end&&s[h]!=':')h++;out->host_length=h-start;
 }
 if(!out->host_length)return 0;
 out->port=port;
 if(end==n||s[end]=='#'){out->path="/";out->path_length=1;}
 else if(s[end]=='?'){out->path=s+end;out->path_length=n-end;}
 else {size_t pe=end;while(pe<n&&s[pe]!='#')pe++;out->path=s+end;out->path_length=pe-end;}
 return 1;
}
#endif
