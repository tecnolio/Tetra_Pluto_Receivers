/* Persistent shared aliases. HTTP server is single-threaded: per-entry updates serialize. */
#define TETRA_NAMES_PATH "/mnt/mmc/tetra-names.tsv"
#define TETRA_NAMES_LIMIT 2048
/* Value length is UTF-8 bytes; the UI permits 80 characters. Empty values are tombstones. */
typedef struct {char kind;int mcc,mnc,id;char value[241];} TetraName;
static TetraName tetra_names[TETRA_NAMES_LIMIT];
static int tetra_name_count,tetra_names_loaded;
static int names_storage_failed;
extern int fileno(FILE*);
extern int fsync(int);
extern int open(const char*,int,...);
static const char*names_path(void){const char*p=getenv("TETRA_NAMES_FILE");return p&&*p?p:TETRA_NAMES_PATH;}
static void names_load(void){
 if(tetra_names_loaded)return;tetra_names_loaded=1;
 FILE*f=fopen(names_path(),"r");if(!f)return;
 char line[320];while(tetra_name_count<TETRA_NAMES_LIMIT&&fgets(line,sizeof(line),f)){
  TetraName x={0};int n=sscanf(line,"%c %d %d %d %240[^\n]",&x.kind,&x.mcc,&x.mnc,&x.id,x.value);
  if(n>=4&&(x.kind=='S'||x.kind=='G')&&x.id>0&&x.id<=16777215)tetra_names[tetra_name_count++]=x;
 }fclose(f);
}
static int names_save(void){
 char temporary[1024];if(snprintf(temporary,sizeof(temporary),"%s.new",names_path())>=(int)sizeof(temporary))return 0;
 FILE*f=fopen(temporary,"w");if(!f)return 0;int ok=1;
 for(int i=0;i<tetra_name_count;i++){TetraName*x=&tetra_names[i];if(fprintf(f,"%c %d %d %d %s\n",x->kind,x->mcc,x->mnc,x->id,x->value)<0)ok=0;}
 if(fflush(f)||fsync(fileno(f)))ok=0;
 if(fclose(f))ok=0;
 if(!ok){remove(temporary);return 0;}
 if(rename(temporary,names_path()))return 0;
 char directory[1024];snprintf(directory,sizeof(directory),"%s",names_path());char*slash=0;
 for(char*p=directory;*p;p++)if(*p=='/')slash=p;
 if(slash){if(slash==directory)slash[1]=0;else *slash=0;}else snprintf(directory,sizeof(directory),".");
 int parent=open(directory,0);if(parent<0){names_storage_failed=1;return 0;}
 int result=fsync(parent);close(parent);if(result){names_storage_failed=1;return 0;}return 1;
}
static int hex_digit(int c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int decode_name(const char*raw,char*out){
 int n=0;while(*raw){int c=(unsigned char)*raw++;if(c=='%'){int a=hex_digit(raw[0]);if(!raw[0])return 0;int b=hex_digit(raw[1]);if(a<0||b<0)return 0;c=a*16+b;raw+=2;}else if(c=='+')c=' ';
 if(c<32||c==127||n>=240)return 0;out[n++]=(char)c;}
 while(n&&out[n-1]==' ')n--;out[n]=0;return 1;
}
static int name_quote(char*out,const char*s){int n=0;out[n++]='"';while(*s){unsigned char c=*s++;if(c=='"'||c=='\\')out[n++]='\\';out[n++]=c;}out[n++]='"';out[n]=0;return n;}
static void tetra_names_api(int fd,const char*q){
 if(names_storage_failed){const char*b="{\"error\":\"alias_storage_recovery_required\"}";reply(fd,503,"application/json",b,(int)strlen(b));return;}
 names_load();char kind[16]={0};query_text(q,"kind",kind,sizeof(kind));
 if(kind[0]){
  char type=!strcmp(kind,"ssi")?'S':!strcmp(kind,"group")?'G':0;
  int id=query_int(q,"id",0),mcc=query_int(q,"mcc",0),mnc=query_int(q,"mnc",0),only_missing=query_int(q,"if_absent",0);
  char raw[1024]={0},value[241]={0};query_text(q,"name",raw,sizeof(raw));
  if(!type||id<=0||id>16777215||mcc<0||mcc>1023||mnc<0||mnc>16383||!decode_name(raw,value)){reply(fd,400,"application/json","{\"error\":\"invalid_alias\"}",25);return;}
  if(type=='S')mcc=mnc=0;
  int i;for(i=0;i<tetra_name_count;i++){TetraName*x=&tetra_names[i];if(x->kind==type&&x->id==id&&x->mcc==mcc&&x->mnc==mnc)break;}
  if(i==tetra_name_count&&i==TETRA_NAMES_LIMIT){const char*b="{\"error\":\"alias_limit\"}";reply(fd,409,"application/json",b,(int)strlen(b));return;}
  int expected_exists=query_int(q,"expected_exists",-1);
  if(expected_exists!=-1&&expected_exists!=0&&expected_exists!=1){const char*b="{\"error\":\"invalid_alias_condition\"}";reply(fd,400,"application/json",b,(int)strlen(b));return;}
  int same=i<tetra_name_count&&!strcmp(tetra_names[i].value,value);
  if(!same&&!(only_missing&&i<tetra_name_count)&&expected_exists>=0){
   char raw_expected[1024]={0},expected[241]={0};query_text(q,"expected_name",raw_expected,sizeof(raw_expected));
   if(!decode_name(raw_expected,expected)){const char*b="{\"error\":\"invalid_alias_condition\"}";reply(fd,400,"application/json",b,(int)strlen(b));return;}
   if((i<tetra_name_count)!=expected_exists||(expected_exists&&strcmp(tetra_names[i].value,expected))){const char*b="{\"error\":\"alias_conflict\"}";reply(fd,409,"application/json",b,(int)strlen(b));return;}
  }
  if(!same&&!(only_missing&&i<tetra_name_count)){
   TetraName previous=tetra_names[i];int previous_count=tetra_name_count;
   TetraName*x=&tetra_names[i];x->kind=type;x->id=id;x->mcc=mcc;x->mnc=mnc;memcpy(x->value,value,strlen(value)+1);
   if(i==tetra_name_count)tetra_name_count++;
   if(!names_save()){tetra_names[i]=previous;tetra_name_count=previous_count;const char*b="{\"error\":\"alias_save_failed\"}";reply(fd,500,"application/json",b,(int)strlen(b));return;}
  }
 }
 static char json[1200000];int n=snprintf(json,sizeof(json),"{\"ssi\":{");
 for(int type=0;type<2;type++){
  if(type)n+=snprintf(json+n,sizeof(json)-n,"},\"groups\":{");int comma=0;
  for(int i=0;i<tetra_name_count;i++){TetraName*x=&tetra_names[i];if(x->kind!=(type?'G':'S'))continue;
   n+=snprintf(json+n,sizeof(json)-n,type?"%s\"%d:%d|GSSI:%d\":" : "%s\"%d\":",comma?",":"",type?x->mcc:x->id,x->mnc,x->id);
   n+=name_quote(json+n,x->value);comma=1;
  }
 }
 n+=snprintf(json+n,sizeof(json)-n,"},\"conditional_updates\":true}");reply(fd,200,"application/json",json,n);
}
