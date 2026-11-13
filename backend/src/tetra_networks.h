/* Shared network profiles. Single HTTP owner serializes atomic SD updates. */
#ifndef TETRA_NETWORKS_PATH
#define TETRA_NETWORKS_PATH "/mnt/mmc/tetra-networks.tsv"
#define TETRA_PAIR_PATH "/mnt/mmc/tetra-active-pair.conf"
#endif
#define NETWORK_MAX 32
#include "tetra_gain_policy.h"
extern int fsync(int);extern int fileno(FILE*);
typedef struct {char id[49],name[193];int ul,dl;char uls[192],dls[192];} TetraNetwork;
static TetraNetwork networks[NETWORK_MAX];static int network_count;
static int network_list(const char*s,char*out,int wanted){
 int a[16],n=0,found=0;const char*p=s;if(!*p)return 0;
 while(*p){if(*p<'0'||*p>'9'||n==16)return 0;char*end=0;unsigned long hz=strtoul(p,&end,10);if(hz<70000000||hz>1000000000)return 0;
  int duplicate=0;for(int j=0;j<n;j++)if(a[j]==(int)hz)duplicate=1;
  if(!duplicate)a[n++]=(int)hz;if((int)hz==wanted)found=1;
  p=end;if(!*p)break;if(*p++!=','||!*p)return 0;
 }
 for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(a[j]<a[i]){int t=a[i];a[i]=a[j];a[j]=t;}
 int used=0;for(int i=0;i<n;i++)used+=snprintf(out+used,192-used,"%s%d",i?",":"",a[i]);return found;
}
static int network_valid(TetraNetwork*x){
 if(!x->id[0]||!x->name[0]||x->ul<70000000||x->ul>990000000||x->dl<80000000||x->dl>1000000000||x->dl-x->ul!=10000000)return 0;
 for(int i=0;x->id[i];i++){char c=x->id[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;}
 for(int i=0;x->name[i];i++)if((unsigned char)x->name[i]<32||x->name[i]==127)return 0;
 char u[192],d[192];if(!network_list(x->uls,u,x->ul)||!network_list(x->dls,d,x->dl))return 0;
 memcpy(x->uls,u,strlen(u)+1);memcpy(x->dls,d,strlen(d)+1);return 1;
}
static void networks_load(void){
 network_count=0;FILE*f=fopen(TETRA_NETWORKS_PATH,"r");char line[1024];
 if(f){while(network_count<NETWORK_MAX&&fgets(line,sizeof(line),f)){TetraNetwork x={0};if(sscanf(line,"%48[^\t]\t%192[^\t]\t%d\t%d\t%191[^\t]\t%191[^\n]",x.id,x.name,&x.ul,&x.dl,x.uls,x.dls)==6&&network_valid(&x))networks[network_count++]=x;}fclose(f);}
}
static int networks_save(void){
 FILE*f=fopen(TETRA_NETWORKS_PATH ".new","w");if(!f)return 0;int ok=1;
 for(int i=0;i<network_count;i++){TetraNetwork*x=&networks[i];if(fprintf(f,"%s\t%s\t%d\t%d\t%s\t%s\n",x->id,x->name,x->ul,x->dl,x->uls,x->dls)<0)ok=0;}
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;
 if(!ok){remove(TETRA_NETWORKS_PATH ".new");return 0;}return !rename(TETRA_NETWORKS_PATH ".new",TETRA_NETWORKS_PATH);
}
static void network_active(char*id,int*ul,int*dl){
 snprintf(id,49,"unconfigured");*ul=0;*dl=0;FILE*f=fopen(TETRA_PAIR_PATH,"r");if(!f)return;
 char key[49];int u,d;if(fscanf(f,"%d %d %48s",&u,&d,key)==3&&u>=70000000&&d<=1000000000&&d-u==10000000){*ul=u;*dl=d;snprintf(id,49,"%s",key);}fclose(f);
}
static int network_pair_write(const char*path,int ul,int dl,const char*id){
 FILE*f=fopen(path,"w");if(!f)return 0;int ok=fprintf(f,"%d %d %s\n",ul,dl,id)>0;if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;return ok;
}
static void network_error(int fd,int status,const char*error){char out[192];int n=snprintf(out,sizeof(out),"{\"error\":\"%s\"}",error);reply(fd,status,"application/json",out,n);}
static int network_param(const char*q,const char*key,char*out,int cap){char raw[1024]={0},value[241]={0};query_text(q,key,raw,sizeof(raw));if(!decode_name(raw,value)||strlen(value)>=(unsigned)cap)return 0;memcpy(out,value,strlen(value)+1);return 1;}
static void tetra_networks_api(int fd,const char*q,int post){
 char action[24]="list";query_text(q,"action",action,sizeof(action));
 if(strcmp(action,"list")&&!post){network_error(fd,405,"post_required");return;}
 networks_load();char active[49];int au,ad;network_active(active,&au,&ad);
 if(strcmp(action,"list")){
  if(!access("runtime/tetra-network-apply",0)){network_error(fd,409,"network_apply_in_progress");return;}
  char id[49]={0};if(!network_param(q,"id",id,sizeof(id))||!id[0]){network_error(fd,400,"invalid_network_id");return;}
  int index=0;for(;index<network_count;index++)if(!strcmp(id,networks[index].id))break;
  if(!strcmp(action,"save")){
   TetraNetwork x={0};snprintf(x.id,sizeof(x.id),"%s",id);x.ul=query_int(q,"ul",0);x.dl=query_int(q,"dl",0);
   if(!network_param(q,"name",x.name,sizeof(x.name))||!network_param(q,"uls",x.uls,sizeof(x.uls))||!network_param(q,"dls",x.dls,sizeof(x.dls))||!network_valid(&x)){network_error(fd,400,"invalid_network_or_pair_10mhz_required");return;}
   if(index==network_count&&network_count==NETWORK_MAX){network_error(fd,409,"network_limit");return;}
   networks[index]=x;if(index==network_count)network_count++;
   if(!networks_save()){network_error(fd,500,"network_save_failed");return;}
  }else if(!strcmp(action,"delete")){
   if(index==network_count){network_error(fd,404,"network_not_found");return;}
   if(network_count==1){network_error(fd,409,"keep_one_network");return;}
   if(!strcmp(active,id)){network_error(fd,409,"apply_another_network_before_delete");return;}
   memmove(networks+index,networks+index+1,(network_count-index-1)*sizeof(TetraNetwork));network_count--;
   if(!networks_save()){network_error(fd,500,"network_save_failed");return;}
  }else if(!strcmp(action,"apply")){
   if(index==network_count){network_error(fd,404,"network_not_found");return;}
   if(dmr_pid()||(tetra_pid()&&!fpga_web_active())){network_error(fd,409,"rf_busy_other_receiver");return;}
   if(access("tetra-network-apply.sh",1)){network_error(fd,503,"network_controller_not_installed");return;}
   if(mkdir("runtime/tetra-network-apply",0700)){network_error(fd,409,"network_apply_in_progress");return;}
   if(!network_pair_write("runtime/tetra-network-apply/previous.conf",au,ad,active)){system("rmdir runtime/tetra-network-apply");network_error(fd,500,"network_backup_failed");return;}
   TetraNetwork*x=&networks[index];
   if(!network_pair_write(TETRA_PAIR_PATH ".new",x->ul,x->dl,x->id)||rename(TETRA_PAIR_PATH ".new",TETRA_PAIR_PATH)){system("rm -f runtime/tetra-network-apply/previous.conf; rmdir runtime/tetra-network-apply");network_error(fd,500,"network_pair_save_failed");return;}
   int gain=tetra_start_gain(tetra_pid()!=0,"/mnt/mmc/tetra-profile/gain.conf","/sys/bus/iio/devices/iio:device0/in_voltage0_hardwaregain");
   FILE*f=fopen("runtime/tetra-network-apply/gain","w");if(f){fprintf(f,"%d\n",gain);fclose(f);}
   int child=fork();if(child==0){close_inherited_fds();execl("/bin/sh","sh","./tetra-network-apply.sh",(char*)0);_exit(127);}
   if(child<0){rename("runtime/tetra-network-apply/previous.conf",TETRA_PAIR_PATH);system("rm -f runtime/tetra-network-apply/gain; rmdir runtime/tetra-network-apply");network_error(fd,503,"network_apply_start_failed");return;}
  }else{network_error(fd,400,"unknown_network_action");return;}
 }
 network_active(active,&au,&ad);static char out[50000];char quoted[400];int n=snprintf(out,sizeof(out),"{\"active_id\":\"%s\",\"applied_ul_hz\":%d,\"applied_dl_hz\":%d,\"applying\":%s,\"profiles\":[",active,au,ad,access("runtime/tetra-network-apply",0)?"false":"true");
 for(int i=0;i<network_count;i++){TetraNetwork*x=&networks[i];name_quote(quoted,x->name);n+=snprintf(out+n,sizeof(out)-n,"%s{\"id\":\"%s\",\"name\":%s,\"ul\":%d,\"dl\":%d,\"uls\":[%s],\"dls\":[%s]}",i?",":"",x->id,quoted,x->ul,x->dl,x->uls,x->dls);}
 char state[96]="";FILE*sf=fopen("runtime/tetra-network-apply.json","r");if(sf){fgets(state,sizeof(state),sf);fclose(sf);}if(!strstr(state,"{\"state\":\""))snprintf(state,sizeof(state),"null");
 n+=snprintf(out+n,sizeof(out)-n,"],\"apply_status\":%s}",state);reply(fd,200,"application/json",out,n);
}
