/* Bounded in-memory browser sessions: no RF work or filesystem writes. */
extern long time(long*);
extern int getpeername(int,void*,unsigned int*);
struct web_session {char id[49],ip[20],app[12];long first,last;int visible;};
static struct web_session web_sessions[128];
#define WEB_SESSION_TTL 120
static int web_app_valid(const char *app){return !strcmp(app,"launchpad")||!strcmp(app,"tetra")||!strcmp(app,"dmr")||!strcmp(app,"tetrapol")||!strcmp(app,"adsb");}
/* Only the local TLS proxy may supply the original IPv4 client address. */
static void web_proxy_client_ip(const char *headers,char *ip,int cap){
 if(strcmp(ip,"127.0.0.1"))return;
 const char *key="x-forwarded-for:";
 for(const char *line=headers;line&&*line;){
  int i=0;while(key[i]&&line[i]){char ch=line[i];if(ch>='A'&&ch<='Z')ch+=32;if(ch!=key[i])break;i++;}
  if(!key[i]){
   const char *v=line+i;while(*v==' '||*v=='\t')v++;
   char value[20];int n=0;
   while(v[n]&&v[n]!='\r'&&v[n]!='\n'&&n<19){value[n]=v[n];n++;}value[n]=0;
   unsigned int a,b,c,d;char tail;
   if((!v[n]||v[n]=='\r'||v[n]=='\n')&&sscanf(value,"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)==4&&a<256&&b<256&&c<256&&d<256)
    snprintf(ip,cap,"%u.%u.%u.%u",a,b,c,d);
   return;
  }
  const char *next=strstr(line,"\n");line=next?next+1:0;
 }
}
static void web_clients(int c,const char *q,const char *headers){
 long now=time(0);char id[64]="",app[20]="";int slot=-1;
 for(int i=0;i<128;i++)if(web_sessions[i].last && now-web_sessions[i].last>WEB_SESSION_TTL)memset(&web_sessions[i],0,sizeof(web_sessions[i]));
 if(query_text(q,"id",id,sizeof(id))){
  int n=(int)strlen(id),valid=n>=8&&n<=48;
  for(int i=0;i<n;i++)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='A'&&id[i]<='Z')||(id[i]>='0'&&id[i]<='9')||id[i]=='-'))valid=0;
  struct addr peer;unsigned int len=sizeof(peer);char ip[20]="";
  if(getpeername(c,&peer,&len)){reply(c,400,"application/json","{}",2);return;}
  unsigned char *b=(unsigned char*)&peer.ip;snprintf(ip,sizeof(ip),"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
  web_proxy_client_ip(headers,ip,sizeof(ip));
  query_text(q,"app",app,sizeof(app));
  if(!valid||!web_app_valid(app)){reply(c,400,"application/json","{}",2);return;}
  for(int i=0;i<128;i++)if(!strcmp(web_sessions[i].id,id)&&!strcmp(web_sessions[i].ip,ip)){slot=i;break;}
  if(query_int(q,"leave",0)){
   if(slot>=0)memset(&web_sessions[slot],0,sizeof(web_sessions[slot]));
  }else{
   if(slot<0)for(int i=0;i<128;i++)if(!web_sessions[i].last){slot=i;break;}
   if(slot<0){const char*b="{\"error\":\"session_limit\"}";reply(c,503,"application/json",b,(int)strlen(b));return;}
   struct web_session *s=&web_sessions[slot];
   if(!s->last)s->first=now;
   snprintf(s->id,sizeof(s->id),"%s",id);snprintf(s->ip,sizeof(s->ip),"%s",ip);snprintf(s->app,sizeof(s->app),"%s",app);s->last=now;s->visible=query_int(q,"visible",1)!=0;
  }
  reply(c,200,"application/json","{\"ok\":true}",11);return;
 }
 char body[32768];int n=snprintf(body,sizeof(body),"{\"ttl_seconds\":120,\"clients\":[");int count=0;
 for(int i=0;i<128;i++){
  struct web_session *s=&web_sessions[i];if(!s->last)continue;
  n+=snprintf(body+n,sizeof(body)-n,"%s{\"id\":\"%s\",\"ip\":\"%s\",\"app\":\"%s\",\"visible\":%s,\"connected_seconds\":%ld,\"age_seconds\":%ld}",count++?",":"",s->id,s->ip,s->app,s->visible?"true":"false",now-s->first,now-s->last);
 }
 n+=snprintf(body+n,sizeof(body)-n,"]}");reply(c,200,"application/json",body,n);
}
static int reboot_header(const char *headers){
 const char *wanted="x-pluto-action: reboot";
 for(const char *line=headers;line&&*line;){
  int i=0;while(wanted[i]&&line[i]){char ch=line[i];if(ch>='A'&&ch<='Z')ch+=32;if(ch!=wanted[i])break;i++;}
  if(!wanted[i]&&(line[i]=='\r'||line[i]=='\n'))return 1;
  const char *next=strstr(line,"\n");line=next?next+1:0;
 }
 return 0;
}
static void web_reboot(int c,int post,const char *headers,const char *q){
 if(!post||!reboot_header(headers)){const char*b="{\"error\":\"POST_required\"}";reply(c,405,"application/json",b,(int)strlen(b));return;}
 if(query_int(q,"dry_run",0)){const char *b="{\"ok\":true,\"dry_run\":true}";reply(c,200,"application/json",b,(int)strlen(b));return;}
 int p=fork();
 if(p<0){const char*b="{\"error\":\"fork_failed\"}";reply(c,503,"application/json",b,(int)strlen(b));return;}
 if(!p){close_inherited_fds();sleep(2);system("sync");execl("/sbin/reboot","reboot",(char*)0);_exit(127);}
 const char*b="{\"ok\":true,\"rebooting\":true}";reply(c,200,"application/json",b,(int)strlen(b));
}
