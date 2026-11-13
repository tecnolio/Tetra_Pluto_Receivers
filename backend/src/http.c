/*
 * Pluto Lab HTTP server v0.3
 *
 * - Launchpad siempre disponible
 * - Control TETRA / DMR
 * - ExclusiÃ³n del AD9361
 * - PID files
 * - API de estado
 *
 * Los receptores se ejecutan como procesos independientes.
 */

#include "platform.h"
extern int project_web(int,const char*,size_t);
extern char *getenv(const char*);
extern void tetrapol_release_rf(void);

extern int socket(int,int,int);
extern int bind(int,const void*,unsigned int);
extern int listen(int,int);
extern int accept(int,void*,void*);
extern int close(int);
extern int setsockopt(int,int,int,const void*,unsigned int);
extern ssize_t recv(int,void*,size_t,int);
extern ssize_t send(int,const void*,size_t,int);

extern void *signal(int,void*);
extern int fork(void);
extern int execl(const char*,const char*,...);
extern int kill(int,int);
extern int waitpid(int,int*,int);
extern int access(const char*,int);
extern int system(const char*);
extern unsigned int sleep(unsigned int);

struct addr {
    unsigned short family;
    unsigned short port;
    unsigned int ip;
    char pad[8];
};

#define TETRA_PID_FILE "runtime/tetra.pid"
#define TETRA_LEGACY_PID_FILE "tetra.pid"
#define DMR_PID_FILE   "runtime/dmr.pid"

static void close_inherited_fds(void)
{
    int fd;
    for(fd=3;fd<64;fd++) close(fd);
}

static int sendall(int fd,const char *b,int n)
{
    while(n>0){
        int k=(int)send(fd,b,n,0x4000);
        if(k<=0) return -1;
        b+=k;
        n-=k;
    }
    return 0;
}

static void reply(int fd,int code,const char *mime,const char *body,int n)
{
    char h[384];

    const char *msg="Error";

    if(code==200) msg="OK";
    else if(code==400) msg="Bad Request";
    else if(code==404) msg="Not Found";
    else if(code==405) msg="Method Not Allowed";
    else if(code==409) msg="Conflict";
    else if(code==503) msg="Service Unavailable";

    int k=snprintf(
        h,sizeof(h),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n"
        "\r\n",
        code,msg,mime,n
    );

    if(sendall(fd,h,k)==0)
        sendall(fd,body,n);
}

/* Credentials live outside the web root, mode 0600. Fail closed if missing. */
static int web_authenticated(const char *request)
{
    char token[256];
    FILE *f=fopen("/mnt/jffs2/pluto_lab/web-auth", "rb");
    if(!f) return 0;
    int ok=fgets(token,sizeof(token),f)!=0;
    fclose(f);
    if(!ok) return 0;
    int len=(int)strlen(token);
    while(len && (token[len-1]=='\n' || token[len-1]=='\r')) token[--len]=0;
    if(!len) return 0;
    const char *p=strstr(request,"\r\n");
    while(p && p[2] && p[2]!='\r'){
        p+=2;
        const char *end=strstr(p,"\r\n");
        if(!end) return 0;
        const char *name="authorization:";
        int i=0;
        for(;name[i] && p+i<end;i++){
            char ch=p[i]; if(ch>='A' && ch<='Z') ch+=32;
            if(ch!=name[i]) break;
        }
        if(!name[i]){
            p+=i;
            while(p<end && (*p==' ' || *p=='\t')) p++;
            if(end-p!=6+len || strncmp(p,"Basic ",6)) return 0;
            unsigned int diff=0;
            for(i=0;i<len;i++) diff|=(unsigned char)p[6+i]^(unsigned char)token[i];
            return diff==0;
        }
        p=end;
    }
    return 0;
}

static int read_pid(const char *file)
{
    FILE *f=fopen(file,"rb");
    int pid=0;

    if(!f) return 0;

    if(fscanf(f,"%d",&pid)!=1)
        pid=0;

    fclose(f);

    if(pid<=1)
        return 0;

    return pid;
}

static void write_pid(const char *file,int pid)
{
    FILE *f=fopen(file,"wb");

    if(!f) return;

    fprintf(f,"%d\n",pid);
    fclose(f);
}

static void clear_pid(const char *file)
{
    remove(file);
}

static int process_alive(int pid)
{
    if(pid<=1)
        return 0;

    /*
     * Primero intentamos recoger un hijo que ya haya terminado.
     *
     * WNOHANG = 1.
     *
     * waitpid:
     *   0   -> sigue ejecutÃ¡ndose
     *   pid -> terminÃ³ y acaba de ser recogido
     *  -1   -> no es hijo nuestro; comprobamos /proc
     */
    int status=0;
    int r=waitpid(pid,&status,1);

    if(r==pid)
        return 0;

    if(r==0)
        return 1;

    /*
     * Si el HTTP se reiniciÃ³, el receptor puede no ser ya
     * hijo directo. Comprobamos /proc/<pid>/stat.
     *
     * Un proceso zombi aparece con estado 'Z'.
     */
    char path[64];
    char line[256];

    snprintf(
        path,sizeof(path),
        "/proc/%d/stat",
        pid
    );

    FILE *f=fopen(path,"rb");

    if(!f)
        return 0;

    int n=(int)fread(
        line,
        1,
        sizeof(line)-1,
        f
    );

    fclose(f);

    if(n<=0)
        return 0;

    line[n]=0;

    /*
     * Formato:
     * PID (nombre) ESTADO ...
     *
     * Buscamos el ')' final y comprobamos el carÃ¡cter
     * de estado que aparece despuÃ©s.
     */
    char *p=line+n-1;

    while(p>line && *p!=')')
        p--;

    if(*p==')'){
        p++;

        while(*p==' ')
            p++;

        if(*p=='Z')
            return 0;
    }

    return kill(pid,0)==0;
}

static int valid_pid(const char *file)
{
    int pid=read_pid(file);

    if(!pid)
        return 0;

    if(!process_alive(pid)){
        clear_pid(file);
        return 0;
    }

    return pid;
}

static int tetra_pid(void)
{
    int pid=valid_pid(TETRA_PID_FILE);

    if(pid)
        return pid;

    return valid_pid(TETRA_LEGACY_PID_FILE);
}

static int dmr_pid(void)
{
    return valid_pid(DMR_PID_FILE);
}

static const char *rf_owner(void)
{
    if(!access("/run/pluto_lab/runtime/tetra-hotspot-active",0))return "hotspot";
    if(tetra_pid()) return "tetra";
    if(dmr_pid()) return "dmr";

    return "idle";
}

static int query_int(const char *q,const char *key,int def)
{
    if(!q) return def;

    int kl=(int)strlen(key);

    for(const char *p=q;*p;){
        if(!strncmp(p,key,kl) && p[kl]=='=')
            return atoi(p+kl+1);

        while(*p && *p!='&')
            p++;

        if(*p=='&')
            p++;
    }

    return def;
}


static int query_text(const char *q,const char *key,char *out,int cap)
{
    if(!q || !out || cap<2) return 0;
    int kl=(int)strlen(key);
    for(const char *p=q;*p;){
        if(!strncmp(p,key,kl) && p[kl]=='='){
            p+=kl+1;
            int n=0;
            while(*p && *p!='&' && n<cap-1) out[n++]=*p++;
            out[n]=0;
            return n;
        }
        while(*p && *p!='&') p++;
        if(*p=='&') p++;
    }
    return 0;
}

#include "web_clients.h"

#include "tetra_names.h"

static int parse_tetra_channels(const char *text,int *count,int *first,int *minf,int *maxf)
{
    int n=0,lo=0,hi=0,one=0;
    const char *p=text;
    if(!p || !*p) return 0;
    while(*p){
        int v=0,digits=0;
        while(*p>='0' && *p<='9'){ v=v*10+(*p-'0'); p++; digits++; }
        if(!digits || v<70000000 || v>1000000000 || n>=16) return 0;
        if(!n){one=lo=hi=v;} else {if(v<lo)lo=v;if(v>hi)hi=v;}
        n++;
        if(!*p) break;
        if(*p!=',') return 0;
        p++;
        if(!*p) return 0;
    }
    if(n<1) return 0;
    *count=n;*first=one;*minf=lo;*maxf=hi;
    return 1;
}

/* ---------------------------------------------------------
 * SYSTEM STATUS
 * --------------------------------------------------------- */

static void system_status(int c)
{
    int tp=tetra_pid();
    int dp=dmr_pid();

    const char *owner=rf_owner();

    char body[256];

    int n=snprintf(
        body,sizeof(body),
        "{"
        "\"http\":true,"
        "\"rf_owner\":\"%s\","
        "\"tetra_running\":%s,"
        "\"tetra_pid\":%d,"
        "\"dmr_running\":%s,"
        "\"dmr_pid\":%d"
        "}",
        owner,
        tp?"true":"false",
        tp,
        dp?"true":"false",
        dp
    );

    reply(c,200,"application/json",body,n);
}

/* ---------------------------------------------------------
 * STOP PROCESS
 * --------------------------------------------------------- */

static int stop_process(const char *pidfile)
{
    int pid=valid_pid(pidfile);

    if(!pid){
        clear_pid(pidfile);
        return 0;
    }

    kill(pid,15);

    /*
     * Hasta 3 segundos.
     * El HTTP sigue siendo simple, pero evitamos wait infinito.
     */
    for(int i=0;i<30;i++){
        if(!process_alive(pid)){
            clear_pid(pidfile);
            return 0;
        }

        usleep(100000);
    }

    return -1;
}

/* ---------------------------------------------------------
 * DMR
 * --------------------------------------------------------- */

/* Shared FPGA lifecycle; the controller restores the stock image on stop. */
extern int mkdir(const char*,unsigned int);
static int fpga_web_active(void){return access("runtime/fpga-web-active",0)==0;}
static int fpga_web_guard(int c){
 if(!fpga_web_active())return 0;
 const char*b="{\"error\":\"tetra_dual_active_stop_before_other_receiver\"}";
 reply(c,409,"application/json",b,(int)strlen(b));return 1;
}
static void fpga_web_request(int c,const char*action){
 char path[96];snprintf(path,sizeof(path),"runtime/fpga-web-active/request");
 FILE*f=fopen(path,"w");if(!f){reply(c,503,"application/json","{}",2);return;}
 fprintf(f,"%s\n",action);fclose(f);
 const char*b="{\"ok\":true,\"state\":\"restoring\"}";reply(c,200,"application/json",b,(int)strlen(b));
}
static void fpga_web_api(int c,const char*q){
 if(access("fpga-web/control.sh",1)){const char*b="{\"available\":false}";reply(c,200,"application/json",b,(int)strlen(b));return;}
 char action[24]="status";query_text(q,"action",action,sizeof(action));
 if(!strcmp(action,"status")){
  char b[1024];FILE*f=fopen("runtime/fpga-web.json","r");int n=f?(int)fread(b,1,sizeof(b),f):0;if(f)fclose(f);
  if(n)reply(c,200,"application/json",b,n);else{const char*t="{\"available\":true,\"active\":false,\"state\":\"ready\"}";reply(c,200,"application/json",t,(int)strlen(t));}return;
 }
 if(!strcmp(action,"restore")||!strcmp(action,"stop")){
  if(fpga_web_active())fpga_web_request(c,action);else reply(c,200,"application/json","{\"ok\":true}",11);return;
 }
 if(strcmp(action,"start")){reply(c,400,"application/json","{}",2);return;}
 if(dmr_pid() || (access("fpga-web/persistent",0)?!tetra_pid():tetra_pid()!=0)){const char*b="{\"error\":\"start_normal_tetra_first\"}";reply(c,409,"application/json",b,(int)strlen(b));return;}
 if(mkdir("runtime/fpga-web-active",0700)){const char*b="{\"error\":\"fpga_trial_already_active\"}";reply(c,409,"application/json",b,(int)strlen(b));return;}
 FILE*f=fopen("runtime/fpga-web.json","w");if(f){fprintf(f,"{\"available\":true,\"active\":true,\"state\":\"starting\"}");fclose(f);}
 int p=fork();
 if(p==0){close_inherited_fds();execl("/bin/sh","sh","./fpga-web/control.sh","run",(char*)0);_exit(127);}
 if(p<0){system("rmdir runtime/fpga-web-active");reply(c,503,"application/json","{}",2);return;}
 const char*b="{\"ok\":true,\"state\":\"starting\"}";reply(c,200,"application/json",b,(int)strlen(b));
}

#include "tetra_networks.h"

static void dmr_start(int c,const char *q)
{
    tetrapol_release_rf();
    if(!access("runtime/tetra-network-apply",0)){network_error(c,409,"network_apply_in_progress");return;}
    if(fpga_web_guard(c))return;
    int dp=dmr_pid();

    if(dp){
        reply(
            c,409,"application/json",
            "{\"error\":\"dmr_already_running\"}",
            31
        );
        return;
    }

    if(tetra_pid()){
        const char *b="{\"error\":\"rf_busy\",\"owner\":\"tetra\"}";
        reply(c,409,"application/json",b,(int)strlen(b));
        return;
    }

    int frequency=query_int(q,"frequency_hz",463250000);
    int gain=query_int(q,"gain",40);
    int rx=query_int(q,"rx",1);
    int voice_mode=query_int(q,"voice",0),cc=query_int(q,"cc",-1),slot=query_int(q,"slot",0);

    if(
        frequency<70000000 ||
        frequency>1000000000 ||
        gain<0 ||
        gain>73 ||
        (rx!=1 && rx!=2) || cc < -1 || cc>15 || slot<0 || slot>2 || (voice_mode!=0&&voice_mode!=1)
    ){
        const char *b="{\"error\":\"invalid_parameters\"}";
        reply(c,400,"application/json",b,(int)strlen(b));
        return;
    }

    if(access("./pluto_dmr",1)){
        const char *b="{\"error\":\"pluto_dmr_not_installed\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    char fs[24];
    char gs[12];
    char rs[8];
    char cs[8],ss[8];snprintf(cs,sizeof(cs),"%d",cc);snprintf(ss,sizeof(ss),"%d",slot);

    snprintf(fs,sizeof(fs),"%d",frequency);
    snprintf(gs,sizeof(gs),"%d",gain);
    snprintf(rs,sizeof(rs),"%d",rx);

    int p=fork();

    if(p<0){
        const char *b="{\"error\":\"fork_failed\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    if(p==0){
        close_inherited_fds();
        execl(
            "./pluto_dmr",
            "pluto_dmr",
            "--frequency",fs,
            "--gain",gs,
            "--rx",rs,
            voice_mode?"--voice":"--extractor","--cc",cs,"--slot",ss,
            query_int(q,"ras",0)?"--ras":"--auto-ras",
            (char*)0
        );

        /*
         * exec sÃ³lo vuelve si falla.
         */
        _exit(127);
    }

    write_pid(DMR_PID_FILE,p);

    char body[192];

    int n=snprintf(
        body,sizeof(body),
        "{"
        "\"running\":true,"
        "\"pid\":%d,"
        "\"frequency_hz\":%d,"
        "\"gain_db\":%d,"
        "\"rx_port\":%d"
        "}",
        p,frequency,gain,rx
    );

    reply(c,200,"application/json",body,n);
}

static int dmr_voice_mode(int pid){
 char path[64],arg[128];snprintf(path,sizeof(path),"/proc/%d/cmdline",pid);FILE*f=fopen(path,"rb");if(!f)return -1;unsigned char ch;int n=0,mode=-1;
 while(fread(&ch,1,1,f)==1){if(ch==0){arg[n]=0;if(!strcmp(arg,"--voice"))mode=1;if(!strcmp(arg,"--extractor"))mode=0;n=0;}else if(n<127)arg[n++]=(char)ch;}
 fclose(f);return mode;
}
static void dmr_stop(int c,const char*q)
{
    int wanted=query_int(q,"voice",-1),pid=dmr_pid();
    if(pid&&wanted>=0&&wanted!=dmr_voice_mode(pid)){const char*b="{\"error\":\"different_dmr_mode_active\"}";reply(c,409,"application/json",b,(int)strlen(b));return;}
    int r=stop_process(DMR_PID_FILE);

    if(r<0){
        const char *b="{\"error\":\"stop_timeout\",\"receiver\":\"dmr\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    const char *b="{\"running\":false}";
    reply(c,200,"application/json",b,(int)strlen(b));
}

static void dmr_process_status(int c)
{
 int p=dmr_pid();char body[160];int mode=p?dmr_voice_mode(p):-1;
 int n=snprintf(body,sizeof(body),"{\"running\":%s,\"pid\":%d,\"mode\":\"%s\"}",p?"true":"false",p,mode==1?"voice":mode==0?"extractor":"idle");
 reply(c,200,"application/json",body,n);
}

/* ---------------------------------------------------------
 * TETRA
 * --------------------------------------------------------- */

static void tetra_start(int c,const char *q)
{
    tetrapol_release_rf();
    if(!access("runtime/tetra-network-apply",0)&&!query_int(q,"profile_apply",0)){network_error(c,409,"network_apply_in_progress");return;}
    if(!access("fpga-web/persistent",0)){
        if(query_int(q,"profile_apply",0)){fpga_web_api(c,"action=start");return;}
        char active[49],request[128];int ul,dl;network_active(active,&ul,&dl);
        snprintf(request,sizeof(request),"action=apply&id=%s",active);
        tetra_networks_api(c,request,1);return;
    }
    if(fpga_web_guard(c))return;
    int tp=tetra_pid();

    if(tp){
        const char *b="{\"error\":\"tetra_already_running\"}";
        reply(c,409,"application/json",b,(int)strlen(b));
        return;
    }

    if(dmr_pid()){
        const char *b="{\"error\":\"rf_busy\",\"owner\":\"dmr\"}";
        reply(c,409,"application/json",b,(int)strlen(b));
        return;
    }

    char channels[256]="0";
    query_text(q,"channels",channels,sizeof(channels));

    int diversity=query_int(q,"diversity",0);
    int channelizer=query_int(q,"channelizer",0);
    int scan=query_int(q,"scan",0);
    int target_ul=query_int(q,"target_ul_hz",0);
    int count=0,first=0,minf=0,maxf=0;
    if(!parse_tetra_channels(channels,&count,&first,&minf,&maxf)){
        const char *b="{\"error\":\"invalid_channels\"}";
        reply(c,400,"application/json",b,(int)strlen(b));
        return;
    }

    int lo=query_int(q,"lo_hz",412550000);
    int rx=query_int(q,"rx",1);
    if(rx<1 || rx>2 || (target_ul&&(target_ul<70000000||target_ul>1000000000)) || (!channelizer && !scan &&
       (lo<70000000 || lo>1000000000 ||
        maxf-minf>2000000 || minf-lo < -1000000 || maxf-lo > 1000000))){
        const char *b="{\"error\":\"invalid_lo_or_rx\"}";
        reply(c,400,"application/json",b,(int)strlen(b));
        return;
    }

    if(diversity&&(diversity!=1||channelizer||(!scan&&count!=1))){const char*b="{\"error\":\"diversity_unsupported_mode\"}";reply(c,400,"application/json",b,(int)strlen(b));return;}
    const char *program=(diversity || channelizer || scan || count>1)?"./pluto_tetra_multi":"./pluto_tetra";
    if(access(program,1)){
        const char *b=count>1?
            "{\"error\":\"pluto_tetra_multi_not_installed\"}":
            "{\"error\":\"pluto_tetra_not_installed\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    char los[24],rs[8],firsts[24],targetuls[24];
    snprintf(los,sizeof(los),"%d",lo);
    snprintf(rs,sizeof(rs),"%d",rx);
    snprintf(firsts,sizeof(firsts),"%d",first);
    snprintf(targetuls,sizeof(targetuls),"%d",target_ul);

    int p=fork();
    if(p<0){
        const char *b="{\"error\":\"fork_failed\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    if(p==0){
        close_inherited_fds();
        if(diversity){
            execl(program,"pluto_tetra_multi","--seconds","86400","--gain","40","--rx","1","--diversity",scan?"--dl-scan":"--direct-ul","--channels",channels,"--target-ul",targetuls,(char*)0);
        }else if(scan){
            execl(program,"pluto_tetra_multi",
                  "--seconds","86400",
                  "--gain","40",
                  "--rx",rs,
                  "--dl-scan",
                  "--channels",channels,
                  "--target-ul",targetuls,
                  (char*)0);
        }else if(channelizer){
            execl(program,"pluto_tetra_multi",
                  "--seconds","86400",
                  "--gain","55",
                  "--rx",rs,
                  "--dl-pair",
                  (char*)0);
        }else if(count>1){
            execl(program,"pluto_tetra_multi",
                  "--seconds","86400",
                  "--gain","40",
                  "--rx",rs,
                  "--lo",los,
                  "--channels",channels,
                  (char*)0);
        }else{
            execl(program,"pluto_tetra",
                  "--seconds","86400",
                  "--gain","40",
                  "--rx",rs,
                  "--lo",los,
                  "--frequency",firsts,
                  (char*)0);
        }
        _exit(127);
    }

    write_pid(TETRA_PID_FILE,p);
    write_pid(TETRA_LEGACY_PID_FILE,p);

    char body[768];
    int n=snprintf(body,sizeof(body),
        "{\"running\":true,\"pid\":%d,\"channels\":\"%s\",\"lo_hz\":%d,\"rx_port\":%d,\"multichannel\":%s,\"channelizer\":%s,\"scan\":%s,\"target_ul_hz\":%d}",
        p,channels,lo,rx,(diversity || channelizer || scan || count>1)?"true":"false",channelizer?"true":"false",scan?"true":"false",target_ul);
    reply(c,200,"application/json",body,n);
}

static void tetra_stop(int c)
{
    if(fpga_web_active()){fpga_web_request(c,"stop");return;}
    int r=stop_process(TETRA_PID_FILE);

    if(r==0)
        r=stop_process(TETRA_LEGACY_PID_FILE);

    if(r<0){
        const char *b="{\"error\":\"stop_timeout\",\"receiver\":\"tetra\"}";
        reply(c,503,"application/json",b,(int)strlen(b));
        return;
    }

    const char *b="{\"running\":false}";
    reply(c,200,"application/json",b,(int)strlen(b));
}

static void tetra_process_status(int c)
{
    int p=tetra_pid();

    char body[128];

    int n=snprintf(
        body,sizeof(body),
        "{\"running\":%s,\"pid\":%d}",
        p?"true":"false",
        p
    );

    reply(c,200,"application/json",body,n);
}

/* ---------------------------------------------------------
 * TETRA HISTORY (events.jsonl)
 * --------------------------------------------------------- */

extern long time(long*);

#include "tetra_history.h"

static void tetra_catalog_api(int c){
 FILE*f=fopen("/mnt/mmc/tetra-groups.tsv","rb");
 if(!f){const char*b="{\"groups\":[],\"persistent\":true}";reply(c,200,"application/json",b,(int)strlen(b));return;}
 fseek(f,0,2);long size=ftell(f);fseek(f,0,0);
 if(size<0||size>8*1024*1024){fclose(f);reply(c,503,"application/json","{}",2);return;}
 int cap=(int)size*16+128;char*out=malloc(cap);
 if(!out){fclose(f);reply(c,503,"application/json","{}",2);return;}
 int used=snprintf(out,cap,"{\"groups\":["),comma=0;char line[160];
 while(fgets(line,sizeof(line),f)){
  int a,b,g,hz;long long first;
  if(sscanf(line,"%d %d %d %d %lld",&a,&b,&g,&hz,&first)!=5||a<1||a>1023||b<1||b>16383||g<1||g>16777215||first<0)continue;
  used+=snprintf(out+used,cap-used,"%s{\"mcc\":%d,\"mnc\":%d,\"gssi\":%d,\"frequency_hz\":%d,\"first_seen_epoch\":%lld}",comma?",":"",a,b,g,hz,first);comma=1;
 }
 fclose(f);used+=snprintf(out+used,cap-used,"],\"persistent\":true}");reply(c,200,"application/json",out,used);free(out);
}

static void tetra_history(int c,const char *q,int positions_only)
{
    int window=query_int(q,"window",1800);
    if(window<0) window=1800;
    if(window==0) window=TETRA_RETENTION_SECONDS;
    if(window>TETRA_RETENTION_SECONDS) window=TETRA_RETENTION_SECONDS;

    long wall=time(0);
    long long cutoff_us=0;
    if(window>0 && wall>1577836800)
        cutoff_us=((long long)wall-window)*1000000LL;

    /* 8 MiB max JSON response; enough for the web UI while bounding RAM. */
    const int cap=8*1024*1024;
    char *body=malloc(cap);
    if(!body){
        reply(c,503,"application/json","[]",2);
        return;
    }

    const char*pc_path=getenv("TETRA_PC_EVENTS_FILE");if(!pc_path||!*pc_path)pc_path="runtime/tetra-pc/events.jsonl";
    FILE*pc=fopen(pc_path,"rb");int has_pc=0;if(pc){fseek(pc,0,2);has_pc=ftell(pc)>0;fclose(pc);}
    int readcap=has_pc?6*1024*1024:cap;
    int used=1;
    body[0]='[';
    /* The journal rebuilds these sparse tmpfs indexes once from SD, then tails
     * the receiver incrementally. A stale/missing heartbeat falls back to SD. */
    int indexed=0;
    if(positions_only==1||positions_only==3||positions_only==4){
        FILE*ready=fopen("history-index/ready-v2","r");long fresh=0;
        if(ready){fscanf(ready,"%ld",&fresh);fclose(ready);}
        indexed=wall>1577836800&&fresh>0&&wall-fresh>=0&&wall-fresh<=5;
    }
    /* Read SD buckets oldest first. Runtime files also cover the journal's
     * sub-second tail; browser merging deduplicates identical event identities. */
    if(wall>1577836800){
        char archive[128];
        if(!indexed)for(long bucket=(wall-window)/3600;bucket<=wall/3600;bucket++){
            snprintf(archive,sizeof(archive),"/mnt/mmc/tetra-history/%ld.jsonl.gz",bucket);
            history_add_file(body,readcap,&used,archive,cutoff_us,positions_only);
        }
        for(long bucket=(wall-window)/TETRA_BUCKET_SECONDS;bucket<=wall/TETRA_BUCKET_SECONDS;bucket++){
            if(indexed)snprintf(archive,sizeof(archive),"history-index/%ld.%d.jsonl",bucket,positions_only);
            else snprintf(archive,sizeof(archive),"/mnt/mmc/tetra-history/%ld.5m.jsonl.gz",bucket);
            history_add_file(body,readcap,&used,archive,cutoff_us,positions_only);
        }
    }
    if(!indexed){
        history_add_file(body,readcap,&used,"events.previous.jsonl",cutoff_us,positions_only);
        history_add_file(body,readcap,&used,"events.jsonl",cutoff_us,positions_only);
    }
    if(has_pc){
        const int pc_cap=2*1024*1024;char*extra=malloc(pc_cap);
        if(extra){int count=1;extra[0]='[';history_add_file(extra,pc_cap,&count,pc_path,cutoff_us,positions_only);
            if(count>1){if(used>1)body[used++]=',';memcpy(body+used,extra+1,count-1);used+=count-1;}free(extra);}
    }
    if(used>=cap-1) used=cap-2;
    body[used++]=']';

    reply(c,200,"application/json",body,used);
    free(body);
}

/* ---------------------------------------------------------
 * STATIC FILE
 * --------------------------------------------------------- */

#include "dmr_http.h"

/* Bounded low-priority history workers keep PCM/status service responsive. */
extern int nice(int);
static int history_workers[4];
static void tetra_history_async(int c,const char*q,int filter,int listener){
 int slot=-1;
 for(int i=0;i<4;i++){
  if(history_workers[i]){int status=0;if(waitpid(history_workers[i],&status,1)!=0)history_workers[i]=0;}
  if(!history_workers[i]&&slot<0)slot=i;
 }
 if(slot<0){reply(c,503,"application/json","[]",2);return;}
 int pid=fork();
 if(pid==0){for(int f=3;f<256;f++)if(f!=c)close(f);nice(15);if(filter>=5)dmr_data_api(c,q,filter);else tetra_history(c,q,filter);close(c);_exit(0);}
 if(pid<0){reply(c,503,"application/json","[]",2);return;}
 history_workers[slot]=pid;
}

static void serve_file(
    int c,
    const char *path,
    const char *file,
    const char *mime,
    int sse
)
{
    FILE *f=fopen(file,"rb");

    if(!f){
        if(!strcmp(path,"/api/dmr/captures")){
            reply(c,200,"application/json","[]",2);
            return;
        }

        reply(
            c,503,
            "application/json",
            "{\"receiver\":\"waiting\"}",
            22
        );
        return;
    }

    fseek(f,0,2);
    long size=ftell(f);
    fseek(f,0,0);

    if(size<0 || size>1100000){
        fclose(f);
        reply(c,503,"text/plain","size limit",10);
        return;
    }

    char *body=malloc(size+96);

    if(!body){
        fclose(f);
        return;
    }

    int prefix=0;

    if(sse)
        prefix=snprintf(
            body,80,
            "retry: 2000\n"
            "event: status\n"
            "data: "
        );

    int got=(int)fread(body+prefix,1,size,f);
    fclose(f);

    if(sse){
        body[prefix+got++]='\n';
        body[prefix+got++]='\n';
    }

    reply(c,200,mime,body,prefix+got);
    free(body);
}

/* ---------------------------------------------------------
 * MAIN
 * --------------------------------------------------------- */

/* Do not let browser preconnections stall PCM while waiting for HTTP bytes. */
struct http_pollfd {int fd;short events,revents;};
extern int poll(struct http_pollfd*,unsigned long,int);
static int next_http_client(int listener){
 static int pending[16];static long born[16];
 for(;;){
  struct http_pollfd fds[17];int slots[17],count=1;long wall=time(0);
  fds[0]=(struct http_pollfd){listener,1,0};
  for(int i=0;i<16;i++){
   if(pending[i]&&wall-born[i]>5){close(pending[i]);pending[i]=0;}
   if(pending[i]){slots[count]=i;fds[count++]=(struct http_pollfd){pending[i],1,0};}
  }
  int ready=poll(fds,(unsigned long)count,1000);if(ready<0)return -1;
  for(int i=1;i<count;i++)if(fds[i].revents){int slot=slots[i],c=pending[slot];pending[slot]=0;return c;}
  if(fds[0].revents&1){
   int c=accept(listener,0,0),slot=-1;if(c<0)continue;
   for(int i=0;i<16;i++)if(!pending[i]){slot=i;break;}
   if(slot<0)close(c);else{pending[slot]=c;born[slot]=wall;}
  }
 }
}

#include "hotspot_rf_guard.h"

static void hotspot_api(int c,const char *q,int post)
{
    const char *script="/mnt/mmc/tetra-hotspot/control.sh";
    if(!post){
        char b[256];
        int n=snprintf(b,sizeof(b),"{\"available\":%s,\"reserved\":%s,\"ready\":%s,\"gadget_running\":%s,\"controller_running\":%s}",
            access(script,0)?"false":"true",
            access("/run/pluto_lab/runtime/tetra-hotspot-active",0)?"false":"true",
            access("/run/pluto_lab/runtime/tetra-hotspot-active/ready",0)?"false":"true",
            valid_pid("/run/tetra-hotspot/gadget.pid")?"true":"false",
            valid_pid("/run/pluto_lab/runtime/hotspot-controller.pid")?"true":"false");
        reply(c,200,"application/json",b,n);return;
    }
    if(access(script,0)){const char*b="{\"error\":\"hotspot_not_installed\"}";reply(c,503,"application/json",b,(int)strlen(b));return;}
    const char *action=q&&!strcmp(q,"action=start")?"start":q&&!strcmp(q,"action=stop")?"stop":q&&!strcmp(q,"action=tx-on")?"tx-on":0;
    if(!action){reply(c,400,"application/json","{}",2);return;}
    if(valid_pid("/run/pluto_lab/runtime/hotspot-controller.pid")){reply(c,409,"application/json","{}",2);return;}
    if(!strcmp(action,"start") && strcmp(rf_owner(),"idle")){reply(c,409,"application/json","{\"error\":\"rf_busy\"}",19);return;}
    int p=fork();
    if(p==0){close_inherited_fds();execl("/bin/sh","sh",script,action,(char*)0);_exit(127);}
    if(p<0){reply(c,503,"application/json","{}",2);return;}
    FILE*f=fopen("/run/pluto_lab/runtime/hotspot-controller.pid","w");if(f){fprintf(f,"%d\n",p);fclose(f);}
    reply(c,202,"application/json","{\"accepted\":true}",17);
}

int main(int argc,char **argv)
{
    int port=8080;

    if(argc==2)
        port=atoi(argv[1]);

    if(port<1024 || port>65535)
        return 2;

    signal(13,(void*)1);

    int fd=socket(2,1,0);

    if(fd<0)
        return 1;

    int on=1;

    setsockopt(fd,1,2,&on,sizeof(on));

    struct addr a={
        2,
        (unsigned short)((port>>8)|((port&255)<<8)),
        0x0100007f, /* Portable backend: loopback only. */
        {0}
    };

    if(bind(fd,&a,sizeof(a)) || listen(fd,8))
        return 1;

    for(;;){

        /* Reap completed asynchronous proxy/history children. Keep the normal
         * SIGCHLD disposition so exec'd services retain working child waits. */
        while(waitpid(-1,0,1)>0){}

        int c=next_http_client(fd);

        if(c<0)
            continue;

        struct {
            long sec,usec;
        } timeout={2,0};

        setsockopt(c,1,20,&timeout,sizeof(timeout));
        setsockopt(c,1,21,&timeout,sizeof(timeout));

        char request[4096];

        int n=(int)recv(
            c,
            request,
            sizeof(request)-1,
            0
        );

        if(n<=0){
            close(c);
            continue;
        }

        request[n]=0;

        while(!strstr(request,"\r\n\r\n") && n<(int)sizeof(request)-1){
            int more=(int)recv(c,request+n,sizeof(request)-1-n,0);
            if(more<=0) break;
            n+=more;request[n]=0;
        }
        if(!strstr(request,"\r\n\r\n") || !web_authenticated(request)){
            const char *denied="HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"Pluto Launchpad\"\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendall(c,denied,(int)strlen(denied));
            close(c);continue;
        }

        /*
         * Compatibilidad con la web actual:
         * comandos y consultas mediante GET.
         */
        if(!access("/run/pluto_lab/runtime/tetra-hotspot-active",0) && hotspot_rf_mutation(request)){
            const char *busy="{\"error\":\"rf_busy\",\"owner\":\"hotspot\"}";
            reply(c,409,"application/json",busy,(int)strlen(busy));close(c);continue;
        }
        if(project_web(c,request,(size_t)n)){close(c);continue;}

        int post=!strncmp(request,"POST ",5);
        if(strncmp(request,"GET ",4) && !post){
            reply(c,405,"text/plain","GET only",8);
            close(c);
            continue;
        }

        char *path=request+(post?5:4);

        int j=0;

        while(
            path[j] &&
            path[j]!=' ' &&
            j<2048
        ) j++;

        const char *headers=path+j+1;
        path[j]=0;

        char *q=0;

        for(int k=0;path[k];k++){
            if(path[k]=='?'){
                path[k]=0;
                q=path+k+1;
                break;
            }
        }

        /* Public package contains only TETRA. Disable unrelated control routes. */
        if(!strncmp(path,"/api/dmr",8)||!strncmp(path,"/api/tetrapol",13)||
           !strncmp(path,"/api/hotspot",12)||!strcmp(path,"/api/system/reboot")){
            reply(c,404,"text/plain","Unavailable",11);close(c);continue;
        }
        if(!strcmp(path,"/api/system/reboot")){
            web_reboot(c,post,headers,q);close(c);continue;
        }
        if(!strcmp(path,"/api/system/clients") && (!post || query_int(q,"leave",0)==1)){
            web_clients(c,q,headers);close(c);continue;
        }
        if(!strcmp(path,"/api/hotspot")){hotspot_api(c,q,post);close(c);continue;}
        if(!strcmp(path,"/api/tetra/networks")){tetra_networks_api(c,q,post);close(c);continue;}
        if(post){reply(c,405,"text/plain","GET only",8);close(c);continue;}

        /* SYSTEM */

        if(!strcmp(path,"/api/system/status")){
            system_status(c);
            close(c);
            continue;
        }

        if(!strcmp(path,"/api/tetra/fpga")){fpga_web_api(c,q);close(c);continue;}

        /* TETRA */

        if(
            !strcmp(path,"/api/start") ||
            !strcmp(path,"/api/tetra/start")
        ){
            tetra_start(c,q);
            close(c);
            continue;
        }

        if(
            !strcmp(path,"/api/stop") ||
            !strcmp(path,"/api/tetra/stop")
        ){
            if(!access("runtime/tetra-network-apply",0)&&!query_int(q,"profile_apply",0)){FILE*f=fopen("runtime/tetra-network-apply/cancel","w");if(f)fclose(f);}
            tetra_stop(c);
            close(c);
            continue;
        }

        if(!strcmp(path,"/api/tetra/status")){
            tetra_process_status(c);
            close(c);
            continue;
        }

        /* DMR */

        if(!strcmp(path,"/api/dmr/start")){
            dmr_start(c,q);
            close(c);
            continue;
        }

        if(!strcmp(path,"/api/dmr/stop")){
            dmr_stop(c,q);
            close(c);
            continue;
        }

        if(!strcmp(path,"/api/dmr/status")){
            dmr_process_status(c);
            close(c);
            continue;
        }

        /* FILE ROUTES */

        const char *file=0;
        char group_audio_path[128];
        const char *mime="application/json";
        int sse=0;

        if(!strcmp(path,"/")){
            file="web/index.html";
            mime="text/html; charset=utf-8";
        }
        else if(!strcmp(path,"/tetra")){
            file="web/tetra.html";
            mime="text/html; charset=utf-8";
        }
        else if(!strcmp(path,"/dmr")){
            file="web/dmr.html";
            mime="text/html; charset=utf-8";
        }
        else if(!strcmp(path,"/adsb")){file="web/adsb.html";mime="text/html; charset=utf-8";}
        else if(!strcmp(path,"/tetrapol")){
            file="web/tetrapol.html";
            mime="text/html; charset=utf-8";
        }
        else if(!strcmp(path,"/web-session.js")){
            file="web/web-session.js";mime="application/javascript; charset=utf-8";
        }
        else if(!strcmp(path,"/app.js")){
            file="web/app.js";
            mime="application/javascript; charset=utf-8";
        }
        else if(!strcmp(path,"/dmr.js")){
            file="web/dmr.js";
            mime="application/javascript; charset=utf-8";
        }
        else if(!strcmp(path,"/tetrapol.js")){
            file="web/tetrapol.js";
            mime="application/javascript; charset=utf-8";
        }
        else if(!strcmp(path,"/style.css")){
            file="web/style.css";
            mime="text/css; charset=utf-8";
        }
        else if(!strcmp(path,"/tetrapol.css")){
            file="web/tetrapol.css";
            mime="text/css; charset=utf-8";
        }
        else if(!strcmp(path,"/leaflet.js")){
            file="web/leaflet.js";
            mime="application/javascript";
        }
        else if(!strcmp(path,"/leaflet.css")){
            file="web/leaflet.css";
            mime="text/css";
        }

        /* TETRA DATA */

        else if(!strcmp(path,"/api/status"))
            file="status.json";

        else if(!strcmp(path,"/api/recent-events"))
            file="events.json";

        else if(!strcmp(path,"/api/tetra/groups")){
            tetra_catalog_api(c);close(c);continue;
        }
        else if(!strcmp(path,"/api/tetra/names")){
            tetra_names_api(c,q);close(c);continue;
        }
        else if(!strcmp(path,"/api/tetra/audio")){
            int g=query_int(q,"gssi",0),a=query_int(q,"mcc",0),b=query_int(q,"mnc",0);
            if(g<1||g>16777215||a<0||a>1023||b<0||b>16383){reply(c,400,"application/json","{}",2);close(c);continue;}
            snprintf(group_audio_path,sizeof(group_audio_path),"runtime/voice-%d-%d-%d.json",a,b,g);
            int stream=0;FILE*f=fopen(group_audio_path,"r");if(f){fscanf(f,"{\"stream_id\":%d",&stream);fclose(f);}
            int pid=tetra_pid();
            if(!pid||stream!=pid){char waiting[256];int n=snprintf(waiting,sizeof(waiting),"{\"stream_id\":%d,\"gssi\":%d,\"mcc\":%d,\"mnc\":%d,\"state\":\"waiting_call\",\"seq\":0,\"frames\":[]}",pid,g,a,b);reply(c,200,"application/json",waiting,n);close(c);continue;}
            file=group_audio_path;
        }
        else if(!strcmp(path,"/api/tetra/listen")){
            /* Compatibility: selection is now local; this endpoint cannot change another listener. */
            const char*b="{\"ok\":true,\"selection\":\"client\"}";reply(c,200,"application/json",b,(int)strlen(b));close(c);continue;
        }
        else if(!strcmp(path,"/api/events")){
            tetra_history_async(c,q,0,fd);
            close(c);
            continue;
        }

        else if(!strcmp(path,"/api/tetra/sds")){tetra_history_async(c,q,4,fd);close(c);continue;}
        else if(!strcmp(path,"/api/tetra/calls")){tetra_history_async(c,q,3,fd);close(c);continue;}
        else if(!strcmp(path,"/api/tetra/uplink")){tetra_history_async(c,q,2,fd);close(c);continue;}

        else if(!strcmp(path,"/api/positions")){
            tetra_history_async(c,q,1,fd);
            close(c);
            continue;
        }

        else if(!strcmp(path,"/api/export")){
            file="events.jsonl";
            mime="application/x-ndjson";
        }

        else if(!strcmp(path,"/api/stream")){
            file="status.json";
            mime="text/event-stream";
            sse=1;
        }

        /* DMR DATA */

        else if(!strcmp(path,"/api/dmr/captures")){tetra_history_async(c,q,5,fd);close(c);continue;}
        else if(!strcmp(path,"/api/dmr/events")){tetra_history_async(c,q,6,fd);close(c);continue;}
        else if(!strcmp(path,"/api/dmr/export")){char format[16]="";query_text(q,"format",format,sizeof(format));tetra_history_async(c,q,!strcmp(format,"csv")?7:8,fd);close(c);continue;}

        else if(!strcmp(path,"/api/dmr/voice"))
            file="runtime/dmr-voice.json";
        else if(!strcmp(path,"/api/dmr/rxstatus"))
            file="dmr_status.json";

        if(!file){
            reply(
                c,404,
                "text/plain",
                "Not found",
                9
            );

            close(c);
            continue;
        }

        serve_file(
            c,
            path,
            file,
            mime,
            sse
        );

        close(c);
    }
}
