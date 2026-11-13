/* Shared RF decoder, independent speech state per call and PCM stream per group. */
#include "tetra_voice.h"
#include "tetra-codec.h"
#include "tetra_catalog.h"
extern long time(long*);
extern int getpid(void);
extern int tetra_cdec(int,short*,short*);
extern int frame_stealing;
#define VOICE_CALLS 32
#define VOICE_GROUPS 32
#define VOICE_RING 12
/* One carrier carries at most four simultaneous logical traffic channels. */
typedef struct {int id,gssi,mcc,mnc,usage,hz,clear,active,speaker,decoded_speaker,control_hz,have_good;uint32_t cell_seed;double seen;tetra_codec*codec;} VoiceCall;
typedef struct {int gssi,mcc,mnc,owner,sequence,good,bad,concealed_frames,frame_seq[VOICE_RING];double seen,last_audio;const char*state;char frames[VOICE_RING][4096];} VoiceGroup;
static VoiceCall calls[VOICE_CALLS];
static VoiceGroup groups[VOICE_GROUPS];
static double polled=-1;
static uint32_t event_seed(const Event*e){uint32_t v=(e->colour_code&63u)|((e->mnc&16383u)<<6)|((e->mcc&1023u)<<20);return(v<<2)|3u;}
static void stream_path(char*dst,int cap,VoiceGroup*g,int temp){snprintf(dst,cap,"runtime/voice-%d-%d-%d.json%s",g->mcc,g->mnc,g->gssi,temp?".new":"");}
static void publish(VoiceGroup*g){
 char path[128],temp[128];stream_path(path,sizeof(path),g,0);stream_path(temp,sizeof(temp),g,1);
 FILE*f=fopen(temp,"w");if(!f)return;
 fprintf(f,"{\"stream_id\":%d,\"gssi\":%d,\"mcc\":%d,\"mnc\":%d,\"call_id\":%d,\"state\":\"%s\",\"seq\":%d,\"good\":%d,\"bad\":%d,\"concealed_frames\":%d,\"sample_rate\":8000,\"frames\":[",getpid(),g->gssi,g->mcc,g->mnc,g->owner,g->state,g->sequence,g->good,g->bad,g->concealed_frames);
 int comma=0;for(int i=g->sequence-VOICE_RING+1;i<=g->sequence;i++){if(i<=0)continue;int k=i%VOICE_RING;if(g->frame_seq[k]!=i)continue;fprintf(f,"%s%s",comma?",":"",g->frames[k]);comma=1;}
 fprintf(f,"]}");fclose(f);rename(temp,path);
}
static VoiceGroup*group_for(VoiceCall*c,double t){
 VoiceGroup*g=0,*old=&groups[0];for(int i=0;i<VOICE_GROUPS;i++){if(groups[i].seen<old->seen)old=&groups[i];if(groups[i].gssi==c->gssi&&groups[i].mcc==c->mcc&&groups[i].mnc==c->mnc)g=&groups[i];}
 if(!g){g=old;if(g->gssi){char path[128];stream_path(path,sizeof(path),g,0);remove(path);}memset(g,0,sizeof(*g));g->gssi=c->gssi;g->mcc=c->mcc;g->mnc=c->mnc;g->state="waiting_call";}
 g->seen=t;return g;
}
static void poll(double t){
 if(polled>=0&&t-polled<0.5)return;polled=t;
 for(int i=0;i<VOICE_GROUPS;i++){VoiceGroup*g=&groups[i];if(g->gssi&&g->last_audio&&t-g->last_audio>1.0&&(!strcmp(g->state,"playing")||!strcmp(g->state,"voice_fec_failed"))){g->state="waiting_call";publish(g);}}
 for(int i=0;i<VOICE_CALLS;i++)if(calls[i].codec&&t-calls[i].seen>120){tetra_codec_destroy(calls[i].codec);calls[i].codec=0;calls[i].active=0;}
}
void voice_event(const Event*e,int hz,double t){
 if(e->gssi>0)catalog_observe(e,hz,time(0));
 poll(t);if(!e->call_event||!e->call_fields_valid||!e->call_id)return;
 if(e->usage_marker>=4&&(e->cmce_type==7||e->allocation_valid))for(int i=0;i<VOICE_CALLS;i++)if(calls[i].id!=e->call_id&&calls[i].usage==e->usage_marker&&calls[i].cell_seed==event_seed(e)&&calls[i].control_hz==hz)calls[i].active=0;
 VoiceCall*c=0,*old=&calls[0];for(int i=0;i<VOICE_CALLS;i++){if(calls[i].seen<old->seen)old=&calls[i];if(calls[i].id==e->call_id&&calls[i].mcc==e->mcc&&calls[i].mnc==e->mnc&&calls[i].cell_seed==event_seed(e)&&calls[i].control_hz==hz)c=&calls[i];}
 if(e->cmce_type==7&&e->gssi){
  if(!c)c=old;
  if(c->id!=e->call_id||c->gssi!=e->gssi||c->mcc!=e->mcc||c->mnc!=e->mnc||!c->active||t-c->seen>120){if(c->codec)tetra_codec_destroy(c->codec);memset(c,0,sizeof(*c));c->usage=-1;}
  c->id=e->call_id;c->gssi=e->gssi;c->cell_seed=event_seed(e);c->control_hz=hz;if(!c->hz)c->hz=hz;c->mcc=e->mcc;c->mnc=e->mnc;c->clear=e->circuit_mode==0&&e->speech_service==0&&e->service_encrypted==0&&!e->encrypted;c->active=1;
 }
 if(!c)return;c->seen=t;if(e->ssi&&(e->cmce_type==7||e->cmce_type==11))c->speaker=e->ssi;
 if(e->usage_marker>=4)c->usage=e->usage_marker;
 if(e->allocation_valid)c->hz=e->allocated_downlink_hz?e->allocated_downlink_hz:hz;
 if(e->cmce_type==6||e->cmce_type==4)c->active=0;
 if(e->service_encrypted==1||e->encrypted)c->clear=0;
 VoiceGroup*g=group_for(c,t);if(e->cmce_type==7||!g->owner)g->owner=c->id;
 if(g->owner==c->id&&(!c->active||!c->clear)){g->state=!c->clear?"encrypted_or_unsupported":"waiting_call";publish(g);}
 else if(!g->sequence)publish(g);
}
/* Systematic shortened RM(30,14), ETSI EN 300 392-2 section 8.2.3.2. */
static const uint16_t parity[14]={0x9b60,0x2de0,0xfc20,0xe03c,0x983a,0x5436,0x2c2e,0xffdf,0x8339,0x42b5,0x21ad,0x1273,0x96b,0x4e7};
static uint32_t encode(int v){uint32_t x=(uint32_t)v<<16;for(int i=0;i<14;i++)if(v&(1<<(13-i)))x^=parity[i];return x;}
static int scramble(uint32_t*s){static const int taps[]={32,26,23,22,16,12,11,10,8,7,5,4,2,1};int b=0;for(int i=0;i<14;i++)b^=*s>>(32-taps[i]);b&=1;*s=(*s>>1)|((uint32_t)b<<31);return b;}
int voice_usage(const uint8_t*raw,uint32_t seed){
 uint32_t x=0;for(int i=0;i<30;i++)x=(x<<1)|(raw[i]^scramble(&seed));
 if(encode(x>>16)==x)return (x>>28)?(x>>22)&63:-1;
 for(int i=0;i<30;i++){uint32_t y=x^(1u<<i);if(encode(y>>16)==y)return(y>>28)?(y>>22)&63:-1;}
 return -1;
}

static void decode_speech(tetra_codec*codec,short*bits,short*pcm){
 uint8_t packed[18]={0};for(int i=0;i<137;i++)packed[i/8]|=(bits[i+1]&1)<<(7-i%8);
 tetra_decode(codec,packed,pcm,bits[0]);
}
int voice_quality(const uint8_t*raw,uint32_t seed,int stealing){
 short in[690]={0},out[276]={0};in[0]=0x6b21;
 for(int i=0;i<432;i++){int bit=raw[i]^scramble(&seed);int idx=i<114?i+1:i<228?i+2:i<342?i+3:i+4;in[idx]=bit?-127:127;}
 frame_stealing=stealing;
 if(tetra_cdec(1,in,out))return 2;
 return (out[138]!=0)+(!stealing&&out[0]!=0);
}
void voice_slot(const uint8_t*raw,uint32_t seed,int usage,int stealing,int hz,int frame,double t){
 poll(t);if(usage<4||frame==18)return;
 VoiceCall*c=0;for(int i=0;i<VOICE_CALLS;i++){VoiceCall*v=&calls[i];if(v->usage==usage&&v->cell_seed==seed&&v->active&&t-v->seen<120&&(!v->hz||v->hz==hz)&&(!c||v->seen>c->seen))c=v;}
 if(!c)return;VoiceGroup*g=group_for(c,t);if(g->owner!=c->id)return;
 if(!c->clear){if(strcmp(g->state,"encrypted_or_unsupported")){g->state="encrypted_or_unsupported";publish(g);}return;}
 if(c->codec&&c->decoded_speaker!=c->speaker){tetra_codec_destroy(c->codec);c->codec=0;c->have_good=0;}
 if(!c->codec){c->codec=tetra_decoder_create();c->decoded_speaker=c->speaker;}
 if(!c->codec){g->state="decoder_unavailable";publish(g);return;}
 short in[690]={0},out[276]={0},pcm[480];in[0]=0x6b21;
 for(int i=0;i<432;i++){int bit=raw[i]^scramble(&seed);int idx=i<114?i+1:i<228?i+2:i<342?i+3:i+4;in[idx]=bit?-127:127;}
 frame_stealing=stealing;
 /* The channel trellis is block-terminated. Initialize its tables, independent of call history. */
 if(tetra_cdec(1,in,out))return;
 int damaged=out[138]||(!stealing&&out[0]);
 if(damaged)g->bad++;else g->good++;
 /* Preserve both 30 ms intervals. BFI uses this call's previous clean parameters. */
 for(int half=0;half<2;half++){
  short*bits=out+138*half;int erased=bits[0]||(stealing&&half==0);
  if(erased){short lost[138]={1};decode_speech(c->codec,lost,pcm+240*half);g->concealed_frames++;
   if(!c->have_good)memset(pcm+240*half,0,240*sizeof(short));
  }else{decode_speech(c->codec,bits,pcm+240*half);c->have_good=1;}
 }
 g->sequence++;g->last_audio=t;g->state="playing";
 int k=g->sequence%VOICE_RING,offset=snprintf(g->frames[k],sizeof(g->frames[k]),"{\"seq\":%d,\"pcm\":[",g->sequence);
 for(int i=0;i<480;i++)offset+=snprintf(g->frames[k]+offset,sizeof(g->frames[k])-offset,"%s%d",i?",":"",pcm[i]);
 snprintf(g->frames[k]+offset,sizeof(g->frames[k])-offset,"]}");g->frame_seq[k]=g->sequence;publish(g);
}
