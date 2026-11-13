/* A durable catalogue is independent of the bounded voice cache and event history. */
#ifndef TETRA_CATALOG_PATH
#define TETRA_CATALOG_PATH "/mnt/mmc/tetra-groups.tsv"
#endif
typedef struct CatalogEntry {int mcc,mnc,gssi,hz;long long first;struct CatalogEntry*next;} CatalogEntry;
static CatalogEntry*catalog_entries;
static int catalog_loaded;
static int catalog_valid(int mcc,int mnc,int id,int hz,long long first){return mcc>0&&mcc<=1023&&mnc>0&&mnc<=16383&&id>0&&id<=16777215&&(hz==0||(hz>=70000000&&hz<=1000000000))&&first>=0;}
static CatalogEntry*catalog_find(int mcc,int mnc,int id){for(CatalogEntry*x=catalog_entries;x;x=x->next)if(x->mcc==mcc&&x->mnc==mnc&&x->gssi==id)return x;return 0;}
static void catalog_load(void){
 if(catalog_loaded)return;catalog_loaded=1;FILE*f=fopen(TETRA_CATALOG_PATH,"r");if(!f)return;
 char line[160];while(fgets(line,sizeof(line),f)){
  int mcc,mnc,id,hz;long long first;
  if(sscanf(line,"%d %d %d %d %lld",&mcc,&mnc,&id,&hz,&first)!=5||!catalog_valid(mcc,mnc,id,hz,first)||catalog_find(mcc,mnc,id))continue;
  CatalogEntry*x=malloc(sizeof(*x));if(!x)break;*x=(CatalogEntry){mcc,mnc,id,hz,first,catalog_entries};catalog_entries=x;
 }fclose(f);
}
static int catalog_save(void){
 FILE*f=fopen(TETRA_CATALOG_PATH ".new","w");if(!f)return 0;int ok=1;
 for(CatalogEntry*x=catalog_entries;x;x=x->next)if(fprintf(f,"%d %d %d %d %lld\n",x->mcc,x->mnc,x->gssi,x->hz,x->first)<0)ok=0;
 if(fflush(f))ok=0;
#ifdef __arm__
 extern int fileno(FILE*);extern int fsync(int);
 if(ok&&fsync(fileno(f)))ok=0;
#endif
 if(fclose(f))ok=0;
 if(!ok){remove(TETRA_CATALOG_PATH ".new");return 0;}
 return rename(TETRA_CATALOG_PATH ".new",TETRA_CATALOG_PATH)==0;
}
static int catalog_observe(const Event*e,int hz,long long wall){
 if(!e->crc_ok||e->direction!=2||!e->call_fields_valid||e->cmce_type!=7||e->destination_kind!=2||!catalog_valid(e->mcc,e->mnc,e->gssi,hz,wall))return 0;
 catalog_load();if(catalog_find(e->mcc,e->mnc,e->gssi))return 1;
 CatalogEntry*x=malloc(sizeof(*x));if(!x)return 0;*x=(CatalogEntry){e->mcc,e->mnc,e->gssi,hz,wall,catalog_entries};catalog_entries=x;
 if(catalog_save())return 1;
 catalog_entries=x->next;free(x);return 0; /* Retry on a later detection, never discard saved entries. */
}
