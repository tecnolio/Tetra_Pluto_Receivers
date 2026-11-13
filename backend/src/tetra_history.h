extern void *gzopen(const char*,const char*);
extern char *gzgets(void*,char*,int);
extern int gzclose(void*);
#include "tetra_history_filter.h"
/* Bounded, whole-event history shared by the HTTP API and its capacity regression test. */
static long long json_epoch_us(const char *line)
{
    const char *p=strstr(line,"\"timestamp_epoch_us\":");
    long long v=0;
    if(!p) return 0;
    p+=21;
    while(*p==' '||*p=='\t')p++;
    if(!strncmp(p,"null",4)) return 0;
    while(*p>='0'&&*p<='9'){
        if(v>922337203685477580LL||(v==922337203685477580LL&&*p>'7'))return 0;
        v=v*10+(*p++-'0');
    }
    return v;
}

static void history_add_file(char *out,int cap,int *used,const char *name,
                             long long cutoff_us,int positions_only)
{
    int compressed=strstr(name,".gz")!=0;
    FILE *f=compressed?(FILE*)gzopen(name,"rb"):fopen(name,"rb");
    char line[16384];
    if(!f) return;

    while(compressed?gzgets(f,line,sizeof(line)):fgets(line,sizeof(line),f)){
        int n=(int)strlen(line);
        while(n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
        /* The receiver may be flushing this regular file concurrently. */
        if(n<2 || line[0]!='{' || line[n-1]!='}') continue;

        /* Sparse position/call queries must filter before the expensive epoch
         * conversion; otherwise each audio poll waits behind full-log sscanf. */
        if(!history_matches(line,positions_only))continue;

        long long ts=json_epoch_us(line);
        if(cutoff_us>0 && ts>0 && ts<cutoff_us) continue;

        int needed=n+(*used>1?1:0)+1;
        if(n+4>=cap) continue;
        if(*used+needed+2>=cap){
            /* Drop whole oldest events, retaining recent data from both log files.
             * Each stored event occupies one line; JSON string newlines are escaped. */
            int drop=*used/4;
            while(drop<*used && out[drop]!='\n') drop++;
            if(drop>=*used) *used=1;
            else {
                drop++;
                if(drop<*used && out[drop]==',') drop++;
                int keep=*used-drop;
                memmove(out+1,out+drop,keep);
                *used=1+keep;
            }
        }
        if(*used>1) out[(*used)++]=',';
        memcpy(out+*used,line,n);*used+=n;
        out[(*used)++]='\n';
    }
    if(compressed)gzclose(f);else fclose(f);
}

