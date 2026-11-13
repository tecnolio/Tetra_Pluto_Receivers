/* DMR SD journal readers. Called only in bounded low-priority HTTP children. */
#ifndef DMR_HTTP_H
#define DMR_HTTP_H
#ifndef DMR_JOURNAL
#define DMR_JOURNAL "/mnt/mmc/dmr-lab/"
#endif
static void dmr_tail_api(int c,const char*q,int events){
 int limit=query_int(q,"limit",100);if(limit<1)limit=1;if(limit>300)limit=300;
 FILE*f=fopen(events?DMR_JOURNAL "events.jsonl":DMR_JOURNAL "captures.jsonl","rb");
 if(!f){reply(c,200,"application/json","[]",2);return;}
 fseek(f,0,2);long size=ftell(f);if(size>524288){fseek(f,size-524288,0);char skip[2048];fgets(skip,sizeof(skip),f);}else fseek(f,0,0);
 char*rows[300]={0};char line[2048];unsigned count=0;
 while(fgets(line,sizeof(line),f)){int n=(int)strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;if(n<2||line[0]!='{'||line[n-1]!='}')continue;
  unsigned i=count%(unsigned)limit;if(rows[i])free(rows[i]);rows[i]=malloc(n+1);if(!rows[i])break;memcpy(rows[i],line,n+1);count++;
 }fclose(f);
 int cap=limit*2048+4;char*out=malloc(cap);if(!out){for(int i=0;i<limit;i++)if(rows[i])free(rows[i]);reply(c,503,"application/json","[]",2);return;}
 unsigned used=1,have=count<(unsigned)limit?count:(unsigned)limit,start=count>=(unsigned)limit?count%(unsigned)limit:0;out[0]='[';
 for(unsigned j=0;j<have;j++){char*r=rows[(start+j)%(unsigned)limit];if(j)out[used++]=',';int n=(int)strlen(r);memcpy(out+used,r,n);used+=n;}
 out[used++]=']';reply(c,200,"application/json",out,used);free(out);for(int i=0;i<limit;i++)if(rows[i])free(rows[i]);
}
static int dmr_json_string(const char*line,const char*key,char*out,int cap){
 char pattern[80];snprintf(pattern,sizeof(pattern),"\"%s\":\"",key);const char*p=strstr(line,pattern);if(!p)return 0;p+=strlen(pattern);int n=0;while(*p&&*p!='"'&&n<cap-1)out[n++]=*p++;out[n]=0;return *p=='"'?n:0;
}
struct dmr_http_tm{int sec,min,hour,mday,mon,year,wday,yday,isdst;};
extern void*gmtime(const long*);
static void dmr_export_api(int c,const char*q,int csv){
 FILE*f=fopen(DMR_JOURNAL "captures.jsonl","rb");
 if(!f){reply(c,200,csv?"text/csv; charset=utf-8":"application/x-ndjson",csv?"FECHA|FRECUENCIA|NUMERO_CAPTURA|MI|AMBE1|AMBE2|AMBE3\n":"",csv?strlen("FECHA|FRECUENCIA|NUMERO_CAPTURA|MI|AMBE1|AMBE2|AMBE3\n"):0);return;}
 const char*h=csv?"HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\nContent-Disposition: attachment; filename=dmr-captures-utc.csv\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nContent-Disposition: attachment; filename=dmr-captures.jsonl\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
 if(sendall(c,h,(int)strlen(h))<0){fclose(f);return;}
 if(csv){const char*header="FECHA|FRECUENCIA|NUMERO_CAPTURA|MI|AMBE1|AMBE2|AMBE3\n";if(sendall(c,header,(int)strlen(header))<0){fclose(f);return;}}
 char line[2048];while(fgets(line,sizeof(line),f)){
  int n=(int)strlen(line);if(n<3||line[0]!='{'||line[n-1]!='\n'||line[n-2]!='}')continue;
  if(!csv){if(sendall(c,line,n)<0)break;continue;}
  if(!strstr(line,"\"valid\":true"))continue;
  if(!query_int(q,"include_ras",0)&&!strstr(line,"\"pi_crc_ok\":true"))continue;
  long long epoch=json_epoch_us(line);long sec=(long)(epoch/1000000);struct dmr_http_tm*t=gmtime(&sec);if(!t)continue;
  unsigned long long number=0;int frequency=0;const char*p=strstr(line,"\"capture_number\":");if(p)sscanf(p+17,"%llu",&number);p=strstr(line,"\"frequency_hz\":");if(p)frequency=atoi(p+15);
  char mi[9],ambe[3][15];if(dmr_json_string(line,"mi",mi,sizeof(mi))!=8)continue;
  p=strstr(line,"\"ambe_frames\":[");if(!p)continue;p+=15;int ok=1;
  for(int i=0;i<3;i++){if(i){if(*p!=','){ok=0;break;}p++;}if(*p++!='"'){ok=0;break;}for(int j=0;j<14;j++)ambe[i][j]=*p++;ambe[i][14]=0;if(*p++!='"'){ok=0;break;}}
  if(!ok)continue;char row[192];int got=snprintf(row,sizeof(row),"%04d_%02d_%02d_%02d:%02d|%d|%llu|%s|%s|%s|%s\n",t->year+1900,t->mon+1,t->mday,t->hour,t->min,frequency,number,mi,ambe[0],ambe[1],ambe[2]);if(sendall(c,row,got)<0)break;
 }fclose(f);
}
static void dmr_data_api(int c,const char*q,int filter){if(filter==5||filter==6)dmr_tail_api(c,q,filter==6);else dmr_export_api(c,q,filter==7);}
#endif
