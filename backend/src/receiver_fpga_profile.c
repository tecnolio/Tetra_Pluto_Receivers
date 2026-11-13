/* Profile-driven programmable FPGA receiver: 1..8 UL and one primary DL. */
#include "core.h"
#include "iio_api.h"
#include "dl_scan_policy.h"
#include "dl_sync_search.h"
#include "tetra_voice.h"
#ifdef __arm__
#include <arm_neon.h>
#endif

extern int iio_channel_attr_read_longlong(const struct iio_channel*,const char*,long long*);
extern int iio_channel_attr_write_longlong(const struct iio_channel*,const char*,long long);
extern int iio_channel_attr_write(const struct iio_channel*,const char*,const char*);
extern int iio_channel_attr_write_double(const struct iio_channel*,const char*,double);
extern unsigned int iio_device_get_channels_count(const struct iio_device*);
extern struct iio_channel *iio_device_get_channel(const struct iio_device*,unsigned int);
extern void iio_channel_disable(struct iio_channel*);
struct data_format {unsigned int length,bits,shift;unsigned char is_signed,is_fully_defined,is_be,with_scale;double scale;unsigned int repeat;};
extern const struct data_format *iio_channel_get_data_format(const struct iio_channel*);
extern int iio_device_set_kernel_buffers_count(const struct iio_device*,unsigned int);
extern void iio_channel_convert(const struct iio_channel*,void*,const void*);
extern int iio_context_set_timeout(struct iio_context*,unsigned int);
extern int clock_gettime(int,void*);
extern long time(long*);
extern void *signal(int,void*);
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t*,const void*,void*(*)(void*),void*);
extern int pthread_join(pthread_t,void**);
extern int sched_yield(void);

#define PI 3.14159265358979323846
#define RF_FS 12000000
#define CLUSTER_FS 2400000
#define OUT_FS 160000
#define RF_BW 12000000
#define LO_HZ 417975000
#define PAIR_RF_FS 2400000
#define OUT_TAPS 31
#define CH_DECIM 15
#define RING 65536
#define MAXEVENT 128
#define EVENTSZ 3072

static volatile int running=1;
static void stop(int sig){(void)sig;running=0;}
static double now(void){struct {long sec,nsec;}t;clock_gettime(1,&t);return t.sec+t.nsec*1e-9;}

typedef struct {
 float x_i[5],x_q[5],s1_i[5],s1_q[5];
 float sum1_i,sum1_q,sum2_i,sum2_q;
 int pos,dec;
} CIC2Dec5;

typedef struct {
 float x_i[CH_DECIM],x_q[CH_DECIM],s1_i[CH_DECIM],s1_q[CH_DECIM];
 float sum1_i,sum1_q,sum2_i,sum2_q;
 int pos,dec;
} CIC2DecCh;

typedef struct {
 float x_i[3],x_q[3],s1_i[3],s1_q[3];
 float sum1_i,sum1_q,sum2_i,sum2_q;
 int pos,dec;
} CIC2Dec3;

typedef struct {
 int frequency;
 CIC2DecCh d10;
 CIC2Dec3 d3;
 float hi[2*OUT_TAPS],hq[2*OUT_TAPS];
 int at;
 IQ ring[RING];
 uint64_t bbcount,out_count;
 double power;
 int wn,warm;
 float initial[200],floor_power;
 long long begin,end;
 long long last_debug;
 double last_dl_sync_event,last_dl_sysinfo_event,last_dl_broadcast_event;
 int gap;
 int afc_hz;
 float afc_phase,afc_step;
 float dyn_cfo,dyn_nco_i,dyn_nco_q,dyn_step_i,dyn_step_q;
 double dyn_cross,dyn_dot;
 int dyn_count,dyn_skip;
 IQ dyn_prev8;
 int dyn_have;
} Channel;

typedef struct {float li,lq,power;} PowerProbe;

#define DL_SBUF 4096
#define DL_RAW 12000
#define DL_BITS 26000
#define DL_RRC_TAPS 141
typedef struct {
 IQ sbuf[DL_SBUF];
 float rrc_i[2*DL_RRC_TAPS],rrc_q[2*DL_RRC_TAPS];
 int sn,have_prev,mapping,locked,have_next,have_cell,rrc_pos,rrc_seen,agc_count;
 float omega,tpos,agc_power,agc_gain;
 IQ prev_sym;
 uint8_t raw[DL_RAW],slot_bits[DL_BITS];
 uint64_t raw_abs0,total_raw,slot_abs0,total_bits,next_slot_abs,next_detect_at;
 uint64_t last_slot_end;
 int have_slot_end;
 uint64_t detect_runs,slot_count,sync_slots,norm1_slots,norm2_slots,fec_attempts,fec_ok;
 uint64_t bsch_attempts,bsch_ok,last_sync_abs;
 int best_sync,best_map,slot_phase,relock_candidate,relock_hits,fec_fail_streak,slot_miss_streak;
 int last_fec_metric,best_fec_metric,last_fec_crc;
 int cell_mcc,cell_mnc,cell_colour,cell_la,cell_band,cell_offset;
 int anchor_fn,anchor_tn,second_stolen;
 uint64_t anchor_abs;
 uint32_t cell_seed;
} DLDecoder;

static CIC2Dec5 ul_dec,dl_dec,pair_front_dec;
static Channel ul[16],dl[2];
static DLDecoder dl_cont[2];
static PowerProbe dp[2];
static float out_taps[OUT_TAPS];
static float rrc_taps[DL_RRC_TAPS];
static float cluster_c[12],cluster_s[12];
static float side_c[100],side_s[100];
static float pair_c[32],pair_s[32];
static int active_selected_ul;
static int ul_freq[8],ul_count;
static int multi_seed_source_dl[8];
static uint32_t multi_seed[8];static int multi_ready[8],multi_colour[8],multi_lip[8],multi_aux_done;static double multi_learned[8];
static float ul_ni[8]={1,1,1,1,1,1,1,1},ul_nq[8],ul_ci[8],ul_cq[8];
static int dl_freq[2]={0,0};
static uint64_t dl_probe_samples;
static char event_json[MAXEVENT][EVENTSZ],position_json[MAXEVENT][EVENTSZ];
static int event_count,position_count,bursts,crc_good,crc_bad,sds_count,lip_count,call_count,failures;
static int ul_crc_ok[8],ul_crc_bad[8],dl_crc_ok[2],dl_crc_bad[2];
static uint64_t input_samples,cluster_samples,output_samples;
static double started;
static const char *mode="live-wideband-v06";
static uint32_t seed=0x3583ae07;
static int rx_port=1,diversity,direct_ul_mode,capture_only,fpga_stage2,fpga_web,fpga_persistent;
static unsigned long long fpga_records,fpga_frames,fpga_meta_errors,fpga_seq_errors,fpga_slot_samples[10];
static int fpga_dl_id=-1,fpga_ul_seed_ready;
#define CAPTURE_SCANS 6000000
static short capture_iq[CAPTURE_SCANS*2];
static unsigned long long div_good[2],div_bad[2],div_selected[2],div_recovered,div_paired,div_single,div_late,div_overflow;
static double div_power[2];
#define DIV_BLOCK 8192
static short div_i[2][DIV_BLOCK],div_q[2][DIV_BLOCK];
static size_t div_count;static uint64_t div_base,div_capture_sample[2];
static volatile int div_job;

static void diversity_ul_offer(Channel*,Event*,double,double);
static void diversity_flush(int);

static int debug_tetra;
static int dl_enabled;
static int dl_afc_hz[2];
static int direct_dl_mode;
static int dl_pair_mode;
static int scan_dl_mode;
#define MAX_SCAN_CHANNELS 16
static int scan_candidates[MAX_SCAN_CHANNELS],scan_count,scan_index,scan_following;
static int scan_discovered_dl[MAX_SCAN_CHANNELS],scan_discovered_ul[MAX_SCAN_CHANNELS],scan_candidate_crc[MAX_SCAN_CHANNELS];
static double scan_candidate_power[MAX_SCAN_CHANNELS];
static int scan_target_ul_hz;
static volatile int scan_follow_hz;
static double scan_tuned_at,scan_last_crc_at;
static int capture_dl_slots;
static void scan_add_candidate(int frequency){
 if(frequency<70000000||frequency>1000000000)return;
 for(int i=0;i<scan_count;i++)if(scan_candidates[i]==frequency)return;
 if(scan_count<MAX_SCAN_CHANNELS)scan_candidates[scan_count++]=frequency;
}
static int active_rf_fs=RF_FS,active_rf_bw=RF_BW,active_lo_hz=LO_HZ;
static float active_out_fs=OUT_FS;
static volatile int event_spin;
static FILE *event_log;
static long event_log_size;

static void spin_lock(volatile int*l){
 while(__sync_lock_test_and_set(l,1))sched_yield();
}
static void spin_unlock(volatile int*l){__sync_lock_release(l);}

static const uint8_t dl_map[8][4]={
 {0,1,2,3},{1,3,0,2},{3,2,1,0},{2,0,3,1},
 {2,3,0,1},{0,2,1,3},{1,0,3,2},{3,1,2,0}
};
static const uint8_t dl_sync_pat[38]={
 1,1,0,0,0,0,0,1,1,0,0,1,1,1,0,0,1,1,1,0,
 1,0,0,1,1,1,0,0,0,0,0,1,1,0,0,1,1,1
};
static const uint8_t dl_n1_pat[22]={1,1,0,1,0,0,0,0,1,1,1,0,1,0,0,1,1,1,0,1,0,0};
static const uint8_t dl_n2_pat[22]={0,1,1,1,1,0,1,0,0,1,0,0,0,0,1,1,0,1,1,1,1,0};
static const uint8_t dl_n3_pat[22]={1,0,1,1,0,1,1,1,0,0,0,0,0,1,1,0,1,0,1,1,0,1};

static void atomic_file(const char*name,const char*body){
 char tmp[96];
 snprintf(tmp,sizeof(tmp),"%s.tmp",name);
 FILE*f=fopen(tmp,"wb");
 if(!f)return;
 fwrite(body,1,strlen(body),f);
 fclose(f);
 rename(tmp,name);
}

static void arrays(void){
 char*out=malloc(MAXEVENT*(EVENTSZ+2)+8);
 if(!out)return;
 const char*names[]={"events.json","positions.json"};
 for(int j=0;j<2;j++){
  int count=j?position_count:event_count;
  char(*src)[EVENTSZ]=j?position_json:event_json;
  int cap=MAXEVENT*(EVENTSZ+2)+8,used=snprintf(out,4,"[");
  for(int k=0;k<count&&k<MAXEVENT;k++){
   int idx=(count>MAXEVENT?count-MAXEVENT:0)+k;
   used+=snprintf(out+used,cap-used,"%s%s",k?",":"",src[idx%MAXEVENT]);
  }
  snprintf(out+used,cap-used,"]");
  atomic_file(names[j],out);
 }
 free(out);
}

/* SPSC queue: UL FEC must never stall DL samples and speech decoding. */
#define FPGA_UL_QUEUE 64
typedef struct {Channel*channel;IQ*samples;int count;long long start;uint32_t seed;} FPGAULJob;
static FPGAULJob fpga_ul_queue[FPGA_UL_QUEUE];
static unsigned int fpga_ul_write,fpga_ul_read;
static unsigned long long fpga_ul_completed,fpga_ul_dropped;
static int fpga_ul_thread_started,fpga_ul_stopping;
/* One producer (DMA) and one consumer (UL DSP); fixed buffers avoid malloc per sample. */
#define UL_STREAM_BLOCK 512
#define UL_STREAM_QUEUE 64
typedef struct {short iq[UL_STREAM_BLOCK][8][2];int count,reset;} ULStreamBlock;
static ULStreamBlock ul_stream_queue[UL_STREAM_QUEUE];
static unsigned int ul_stream_write,ul_stream_read;
static int ul_stream_staged,ul_stream_stopping,fpga_stream_gap=1;
static short fpga_stream_iq[8][2];
static unsigned long long ul_stream_completed,ul_stream_dropped;
static void status(const char*state){
 for(int i=0;i<ul_count;i++)if(multi_ready[i]&&now()-multi_learned[i]>60)__atomic_store_n(&multi_ready[i],0,__ATOMIC_RELEASE);
 char out[6144];
 char channels[96],candidates[256],discovered_dl[256],discovered_ul[256];
 long wall=time(0);
 double elapsed=now()-started;
 DLDecoder*sd=&dl_cont[1];
 if(dl_pair_mode)snprintf(channels,sizeof(channels),"[%d,%d]",dl_freq[0],dl_freq[1]);
 else if(direct_ul_mode){int n=snprintf(channels,sizeof(channels),"[");for(int i=0;i<ul_count;i++)n+=snprintf(channels+n,sizeof(channels)-n,"%s%d",i?",":"",ul_freq[i]);snprintf(channels+n,sizeof(channels)-n,"]");}
 else if(direct_dl_mode)snprintf(channels,sizeof(channels),"[%d]",dl_freq[1]);
 else snprintf(channels,sizeof(channels),"[%d,%d]",active_selected_ul?active_selected_ul:ul_freq[0],dl_freq[1]);
 int used=snprintf(candidates,sizeof(candidates),"[");
 for(int i=0;i<scan_count&&used<(int)sizeof(candidates)-16;i++)
  used+=snprintf(candidates+used,sizeof(candidates)-used,"%s%d",i?",":"",scan_candidates[i]);
 snprintf(candidates+used,sizeof(candidates)-used,"]");
 used=snprintf(discovered_dl,sizeof(discovered_dl),"[");
 for(int i=0;i<scan_count&&used<(int)sizeof(discovered_dl)-16;i++)
  used+=snprintf(discovered_dl+used,sizeof(discovered_dl)-used,"%s%d",i?",":"",scan_discovered_dl[i]);
 snprintf(discovered_dl+used,sizeof(discovered_dl)-used,"]");
 used=snprintf(discovered_ul,sizeof(discovered_ul),"[");
 for(int i=0;i<scan_count&&used<(int)sizeof(discovered_ul)-16;i++)
  used+=snprintf(discovered_ul+used,sizeof(discovered_ul)-used,"%s%d",i?",":"",scan_discovered_ul[i]);
 snprintf(discovered_ul+used,sizeof(discovered_ul)-used,"]");
 const char*implementation=dl_pair_mode?"dual_dl":
  (direct_ul_mode?"ul_narrow_frame_diversity_v1":scan_dl_mode?"dl_sysinfo_target_scan_v2":
   (direct_dl_mode?"direct_dl_grc_equivalent":"profile_ul_primary_dl_v1"));
 snprintf(out,sizeof(out),
  "{\"receiver\":\"%s\",\"mode\":\"%s\",\"clock_valid\":%s,"
  "\"frequency_hz\":%d,\"lo_hz\":%d,\"sample_rate\":%d,\"rf_bandwidth\":%d,"
  "\"output_rate\":%d,\"rx_port\":%d,"
  "\"channels_hz\":%s,\"scan_candidates_hz\":%s,\"scan_discovered_dl_hz\":%s,\"scan_discovered_ul_hz\":%s,"
  "\"scan_state\":\"%s\",\"scan_target_ul_hz\":%d,\"announced_downlink_hz\":%d,"
  "\"ul_channels\":%d,\"dl_channels\":%d,\"debug_tetra\":%s,\"dl_auto_afc\":%s,\"dl_afc_hz\":[%d,%d,%d],\"dl_power\":[%.3f,%.3f,%.3f],"
  "\"dl_crc_ok\":[%d,%d,%d],\"dl_crc_fail\":[%d,%d,%d],"
  "\"dl_best_sync\":[%d,%d],\"dl_locked\":[%s,%s],\"dl_symbols\":[%llu,%llu],"
  "\"dl_slots\":[%llu,%llu],\"dl_fec_ok_detail\":[%llu,%llu],\"dl_detect_runs\":[%llu,%llu],"
  "\"dl_cfo_hz\":[%.3f,%.3f],\"dl_omega\":[%.6f,%.6f],"
  "\"dl_dsp\":{\"symbols\":%llu,\"detect_runs\":%llu,\"best_sync\":%d,\"best_map\":%d,"
  "\"locked\":%s,\"mapping\":%d,\"slots\":%llu,\"sync_slots\":%llu,"
  "\"norm1_slots\":%llu,\"norm2_slots\":%llu,\"fec_ok\":%llu,\"fec_attempts\":%llu,"
  "\"bsch_ok\":%llu,\"bsch_attempts\":%llu,\"last_fec_metric\":%d,"
  "\"best_fec_metric\":%d,\"last_fec_crc\":%d,\"cfo_hz\":%.3f,\"omega\":%.6f},"
  "\"uptime_s\":%.3f,\"samples\":%llu,\"cluster_samples\":%llu,\"output_samples\":%llu,"
  "\"stream_rate\":%.0f,\"bursts\":%d,\"valid_bursts\":%d,\"crc_ok\":%d,\"crc_fail\":%d,"
  "\"sds\":%d,\"lip\":%d,\"calls\":%d,\"positions\":%d,\"iio_errors\":%d,"
  "\"storage\":\"volatile_bounded\",\"implementation\":\"%s\"}",
  state,mode,wall>1577836800?"true":"false",
  (direct_dl_mode||dl_pair_mode||fpga_web)?dl_freq[1]:ul_freq[0],active_lo_hz,active_rf_fs,active_rf_bw,(int)(active_out_fs+.5f),rx_port,channels,candidates,discovered_dl,discovered_ul,
  scan_dl_mode?(scan_following?"following":"searching"):"off",scan_target_ul_hz,scan_follow_hz,
  (direct_dl_mode||dl_pair_mode)?0:ul_count,dl_enabled?(dl_pair_mode?2:1):0,debug_tetra?"true":"false",
  direct_dl_mode?"false":"true",
  dl_afc_hz[0],dl_afc_hz[1],0,
  dp[0].power,dp[1].power,0.0,
  dl_crc_ok[0],dl_crc_ok[1],0,dl_crc_bad[0],dl_crc_bad[1],0,
  dl_cont[0].best_sync,dl_cont[1].best_sync,
  dl_cont[0].locked?"true":"false",dl_cont[1].locked?"true":"false",
  (unsigned long long)dl_cont[0].total_raw,(unsigned long long)dl_cont[1].total_raw,
  (unsigned long long)dl_cont[0].slot_count,(unsigned long long)dl_cont[1].slot_count,
  (unsigned long long)dl_cont[0].fec_ok,(unsigned long long)dl_cont[1].fec_ok,
  (unsigned long long)dl_cont[0].detect_runs,(unsigned long long)dl_cont[1].detect_runs,
  dl[0].dyn_cfo,dl[1].dyn_cfo,dl_cont[0].omega,dl_cont[1].omega,
  (unsigned long long)sd->total_raw,(unsigned long long)sd->detect_runs,
  sd->best_sync,sd->best_map,sd->locked?"true":"false",sd->mapping,
  (unsigned long long)sd->slot_count,(unsigned long long)sd->sync_slots,
  (unsigned long long)sd->norm1_slots,(unsigned long long)sd->norm2_slots,
  (unsigned long long)sd->fec_ok,(unsigned long long)sd->fec_attempts,
  (unsigned long long)sd->bsch_ok,(unsigned long long)sd->bsch_attempts,
  sd->last_fec_metric,sd->best_fec_metric,sd->last_fec_crc,
  dl[1].dyn_cfo,sd->omega,elapsed,
  (unsigned long long)input_samples,(unsigned long long)cluster_samples,
  (unsigned long long)output_samples,input_samples/(elapsed+.000001),
  bursts,crc_good,crc_good,crc_bad,sds_count,lip_count,call_count,position_count,failures,
  implementation);
 size_t div_used=strlen(out);if(div_used&&out[div_used-1]=='}')snprintf(out+div_used-1,sizeof(out)-div_used+1,
 ",\"diversity\":%s,\"diversity_mode\":\"%s\",\"rx_power\":[%.2f,%.2f],\"rx_good\":[%llu,%llu],\"rx_bad\":[%llu,%llu],\"diversity_selected\":[%llu,%llu],\"diversity_recovered\":%llu,\"diversity_paired\":%llu,\"diversity_single\":%llu,\"diversity_late\":%llu,\"diversity_overflow\":%llu}",
 diversity?"true":"false",diversity?"decoded_frame_selection":"off",div_power[0],div_power[1],div_good[0],div_good[1],div_bad[0],div_bad[1],div_selected[0],div_selected[1],div_recovered,div_paired,div_single,div_late,div_overflow);
 size_t n=strlen(out)-1;
 n+=snprintf(out+n,sizeof(out)-n,",\"fpga_records\":%llu,\"fpga_frames\":%llu,\"fpga_metadata_errors\":%llu,\"fpga_sequence_errors\":%llu,\"fpga_ul_queue_depth\":%u,\"fpga_ul_completed\":%llu,\"fpga_ul_dropped\":%llu,\"aux_dl_hz\":%d,\"max_ul_channels\":8",fpga_records,fpga_frames,fpga_meta_errors,fpga_seq_errors,__atomic_load_n(&fpga_ul_write,__ATOMIC_ACQUIRE)-__atomic_load_n(&fpga_ul_read,__ATOMIC_ACQUIRE),fpga_ul_completed,fpga_ul_dropped,dl_freq[0]);
 n+=snprintf(out+n,sizeof(out)-n,",\"fpga_ul_stream_queue_depth\":%u,\"fpga_ul_stream_frames\":%llu,\"fpga_ul_stream_dropped\":%llu",__atomic_load_n(&ul_stream_write,__ATOMIC_ACQUIRE)-__atomic_load_n(&ul_stream_read,__ATOMIC_ACQUIRE),ul_stream_completed,ul_stream_dropped);
 float input_peak[2]={0,0};
 for(int k=0;k<2;k++)for(int j=0;j<OUT_TAPS;j++){
  float a=dl[k].hi[j]<0?-dl[k].hi[j]:dl[k].hi[j],b=dl[k].hq[j]<0?-dl[k].hq[j]:dl[k].hq[j];
  if(a>input_peak[k])input_peak[k]=a;if(b>input_peak[k])input_peak[k]=b;
 }
 n+=snprintf(out+n,sizeof(out)-n,",\"dl_input_peak\":[%.1f,%.1f]",input_peak[0],input_peak[1]);
 const char*keys[]={"ul_frequencies_hz","ul_seed_ready","ul_colour_code","ul_lip","ul_crc_ok","ul_crc_fail","ul_seed_source_dl_hz"};
 const int*values[]={ul_freq,multi_ready,multi_colour,multi_lip,ul_crc_ok,ul_crc_bad,multi_seed_source_dl};
 for(int k=0;k<7;k++){
  n+=snprintf(out+n,sizeof(out)-n,",\"%s\":[",keys[k]);
  for(int j=0;j<ul_count;j++)n+=snprintf(out+n,sizeof(out)-n,"%s%d",j?",":"",values[k][j]);
  n+=snprintf(out+n,sizeof(out)-n,"]");
 }
 n+=snprintf(out+n,sizeof(out)-n,",\"fpga_slot_samples\":[");
 for(int j=0;j<ul_count+2;j++)n+=snprintf(out+n,sizeof(out)-n,"%s%llu",j?",":"",fpga_slot_samples[j]);
 snprintf(out+n,sizeof(out)-n,"]}");
 atomic_file("status.json",out);
}

static void record_event(Channel*c,Event*e,double time_s){
 spin_lock(&event_spin);
 e->time_s=time_s;
 bursts++;
 crc_good+=e->crc_ok;
 crc_bad+=!e->crc_ok;
 for(int i=0;i<ul_count;i++)if(c->frequency==ul_freq[i]){
   if(e->crc_ok){ul_crc_ok[i]++;if(e->lip)multi_lip[i]++;}else ul_crc_bad[i]++;
 }
 for(int i=0;i<2;i++){
  if(c->frequency==dl_freq[i]&&(!direct_dl_mode||i==1)){
    if(e->crc_ok)dl_crc_ok[i]++;else dl_crc_bad[i]++;
  }
 }
  sds_count+=e->sds;
  lip_count+=e->lip;
  call_count+=e->call_event;

  /* MAC-NULL confirms channel health but carries no information for history. */
  if(e->status&&!strcmp(e->status,"MAC-NULL")){
   spin_unlock(&event_spin);
   return;
  }

  if((c==&dl[0]||c==&dl[1])&&e->status){
   double *last=0,interval=0;
   if(!strcmp(e->status,"DL-SYNC-SB1")){last=&c->last_dl_sync_event;interval=5.0;}
   else if(!strcmp(e->status,"DL-SYSINFO")){last=&c->last_dl_sysinfo_event;interval=5.0;}
   else if(!strcmp(e->status,"DL-BROADCAST")){last=&c->last_dl_broadcast_event;interval=2.0;}
   if(last){
    if(*last>0&&time_s-*last<interval){spin_unlock(&event_spin);return;}
    *last=time_s;
   }
  }

 char json[EVENTSZ];
  pt_json(json,sizeof(json),e,c->frequency);

 long wall=time(0);
 int len=(int)strlen(json);
 if(len>0&&json[len-1]=='}'){
  json[len-1]=0;
  snprintf(json+len-1,sizeof(json)-len+1,
   ",\"timestamp_epoch_us\":%llu,\"clock_valid\":%s,\"mode\":\"%s\",\"channel_role\":\"%s\"}",
   (unsigned long long)(wall>1577836800?(uint64_t)wall*1000000:0),
   wall>1577836800?"true":"false",
   mode,
   (c==&dl[0]||c==&dl[1])?"DL":"UL");
 }

 snprintf(event_json[event_count++%MAXEVENT],EVENTSZ,"%s",json);
  if(e->crc_ok&&e->position_valid)
  snprintf(position_json[position_count++%MAXEVENT],EVENTSZ,"%s",json);

 if(!event_log){
  event_log=fopen("events.jsonl","ab");
  if(event_log){
   fseek(event_log,0,2);
   event_log_size=ftell(event_log);
  }
 }
 if(event_log){
  if(event_log_size>16777216){
   fflush(event_log);
   fclose(event_log);
   event_log=0;
   rename("events.jsonl","events.previous.jsonl");
   event_log=fopen("events.jsonl","wb");
   event_log_size=0;
  }
  if(event_log){
   int written=fprintf(event_log,"%s\n",json);
   if(written>0)event_log_size+=written;
  }
 }

 if(debug_tetra)printf("%s\n",json);
 spin_unlock(&event_spin);
}

static int fpga_ul_submit(Channel*c,IQ*x,int n,long long start){
 unsigned int w=__atomic_load_n(&fpga_ul_write,__ATOMIC_RELAXED),r=__atomic_load_n(&fpga_ul_read,__ATOMIC_ACQUIRE);
 if(w-r>=FPGA_UL_QUEUE){fpga_ul_dropped++;free(x);return 0;}
 uint32_t job_seed=0;for(int i=0;i<ul_count;i++)if(c->frequency==ul_freq[i])job_seed=multi_seed[i];
 fpga_ul_queue[w%FPGA_UL_QUEUE]=(FPGAULJob){c,x,n,start,job_seed};
 __atomic_store_n(&fpga_ul_write,w+1,__ATOMIC_RELEASE);return 1;
}
static void*fpga_ul_worker(void*unused){
 (void)unused;
#ifdef __arm__
 /* Linux applies this to the calling worker thread. Preserve ingest/audio
  * and HTTP responsiveness if the configured UL channels saturate the FEC worker. */
 extern int setpriority(int,int,int);setpriority(0,0,5);
#endif
 for(;;){
  unsigned int r=__atomic_load_n(&fpga_ul_read,__ATOMIC_RELAXED),w=__atomic_load_n(&fpga_ul_write,__ATOMIC_ACQUIRE);
  if(r==w){if(__atomic_load_n(&fpga_ul_stopping,__ATOMIC_ACQUIRE))break;usleep(1000);continue;}
  FPGAULJob job=fpga_ul_queue[r%FPGA_UL_QUEUE];Event e;
  pt_demod_ul_rate(job.samples,job.count,job.seed,&e,active_out_fs);
  free(job.samples);record_event(job.channel,&e,job.start/(double)active_out_fs);
  fpga_ul_completed++;
  __atomic_store_n(&fpga_ul_read,r+1,__ATOMIC_RELEASE);
 }
 return 0;
}

static void emit(Channel*c,long long a,long long b){
 if(b-a>RING||a<0||b>(long long)c->bbcount)return;
 int n=(int)(b-a);
 IQ*x=malloc(n*sizeof(IQ));
 if(!x)return;
 for(int i=0;i<n;i++)x[i]=c->ring[(a+i)%RING];

 if(fpga_ul_thread_started){int ready=0;for(int i=0;i<ul_count;i++)if(c->frequency==ul_freq[i])ready=__atomic_load_n(&multi_ready[i],__ATOMIC_ACQUIRE);if(ready)fpga_ul_submit(c,x,n,a);else free(x);return;}
 Event e;

 if(direct_ul_mode||c->frequency==ul_freq[0])pt_demod_ul_rate(x,n,seed,&e,active_out_fs);else pt_demod_rate(x,n,seed,&e,active_out_fs);
 free(x);
 if(diversity&&direct_ul_mode)diversity_ul_offer(c,&e,a/(double)active_out_fs,b/(double)active_out_fs);
 else record_event(c,&e,a/(double)active_out_fs);
}

static int dl_score_at(const uint8_t*b,const uint8_t*p,int pos,int n){
 int s=0;
 for(int i=0;i<n;i++)s+=b[pos+i]==p[i];
 return s;
}

static IQ dl_linsamp(DLDecoder*d,float p){
 int i=(int)p;
 float f=p-i;
 IQ a=d->sbuf[i],b=d->sbuf[i+1];
 return (IQ){a.i*(1-f)+b.i*f,a.q*(1-f)+b.q*f};
}

static uint32_t dl_cell_seed(int mcc,int mnc,int colour){
 uint32_t v=((uint32_t)(colour&0x3f))|((uint32_t)(mnc&0x3fff)<<6)|((uint32_t)(mcc&0x3ff)<<20);
 return ((v<<2)|3u);
}

/* Both carriers belong to the same BSCH/SYSINFO-validated cell. */
static void learn_cell_uplinks(Channel*c,DLDecoder*d,const Event*e){
 if(!e->crc_ok||!e->network_info_valid||!e->status||strcmp(e->status,"DL-SYSINFO")||
    e->duplex_spacing_hz!=10000000||e->reverse_operation||e->downlink_hz<=0||
    e->uplink_hz!=e->downlink_hz-10000000||!d->have_cell||!d->bsch_ok)return;
 for(int i=0;i<ul_count;i++)if(ul_freq[i]==c->frequency-10000000||ul_freq[i]==e->uplink_hz){
  multi_seed[i]=d->cell_seed;multi_colour[i]=d->cell_colour;multi_learned[i]=now();
  multi_seed_source_dl[i]=c->frequency;
  __atomic_store_n(&multi_ready[i],1,__ATOMIC_RELEASE);
  if(c==&dl[0])multi_aux_done=1;
 }
}
static void dl_decode_raw(Channel*c,DLDecoder*d,const uint8_t*raw,int k,uint32_t scramb,int training,double time_s){
 Event e={0};
 d->fec_attempts++;
 int ok=pt_fec(raw,k,scramb,&e);
 d->last_fec_metric=e.metric;
 d->last_fec_crc=e.crc;
 if(d->fec_attempts==1||e.metric<d->best_fec_metric)d->best_fec_metric=e.metric;
 if(k==120)d->bsch_attempts++;
 if(!ok){
  spin_lock(&event_spin);
  crc_bad++;
  for(int i=0;i<2;i++)if(c->frequency==dl_freq[i]&&(!direct_dl_mode||i==1))dl_crc_bad[i]++;
  spin_unlock(&event_spin);
  if(++d->fec_fail_streak>=256){
   d->locked=0;
   d->have_next=0;
   d->next_detect_at=d->total_raw;
   d->fec_fail_streak=0;
  }
  return;
 }
 d->fec_fail_streak=0;
 d->fec_ok++;
 if(k==120)d->bsch_ok++;
 e.direction=2;
 e.training=training;
 e.bit_variant=d->mapping;
 pt_protocol(&e);
 if(scan_dl_mode){
  scan_last_crc_at=now();
  if(!scan_following&&scan_index>=0&&scan_index<scan_count){
   scan_candidate_crc[scan_index]++;
   if(e.network_info_valid&&e.downlink_hz>0){
    scan_discovered_dl[scan_index]=e.downlink_hz;
    scan_discovered_ul[scan_index]=e.uplink_hz;
    scan_add_candidate(e.downlink_hz);
    scan_add_candidate(dl_scan_target_hint(scan_target_ul_hz,e.downlink_hz,e.uplink_hz));
   }
  }
 }
 if(k==120&&e.network_info_valid&&e.mcc>0&&e.mnc>=0){
  d->cell_mcc=e.mcc;
  d->cell_mnc=e.mnc;
  d->cell_colour=e.colour_code;
  d->cell_seed=dl_cell_seed(e.mcc,e.mnc,e.colour_code);
  
  d->have_cell=1;
  d->anchor_fn=e.fn;d->anchor_tn=e.tn;d->anchor_abs=(uint64_t)(time_s*36000.0+0.5);
 }else if(d->have_cell){
  e.mcc=d->cell_mcc;
  e.mnc=d->cell_mnc;
  e.colour_code=d->cell_colour;
 }
 learn_cell_uplinks(c,d,&e);
 if(e.downlink_hz>0){d->cell_band=e.freq_band;d->cell_offset=e.freq_offset;d->cell_la=e.la;}
 if(d->have_cell)e.la=d->cell_la;
 if(e.allocation_valid){static const int offsets[]={0,6250,-6250,12500};int band=e.allocated_band>=0?e.allocated_band:d->cell_band;int off=e.allocated_offset>=0?e.allocated_offset:d->cell_offset;if(band>0)e.allocated_downlink_hz=band*100000000+e.allocated_carrier*25000+offsets[off&3];}
 if(k==216&&training==1&&e.type1_length>=13&&e.mac_kind==0){int length=0;for(int i=7;i<13;i++)length=(length<<1)|e.payload[i];if(length==62)d->second_stolen=1;}
 if(c==&dl[0]){if(k==120||e.downlink_hz>0)record_event(c,&e,time_s);return;}
 voice_event(&e,c->frequency,now());
 record_event(c,&e,time_s);
}

static void dl_process_slot(Channel*c,DLDecoder*d,const uint8_t*slot,uint64_t abs_start){
 int ss=dl_score_at(slot,dl_sync_pat,214,38);
 int n1=dl_score_at(slot,dl_n1_pat,244,22);
 int n2=dl_score_at(slot,dl_n2_pat,244,22);
 int n3=dl_score_at(slot,dl_n3_pat,244,22);
 double time_s=abs_start/36000.0;
 uint8_t raw[432];
 d->slot_count++;

 if(ss>=34){
  d->slot_miss_streak=0;
  d->sync_slots++;
  if(capture_dl_slots&&d->sync_slots<=64){
   spin_lock(&event_spin);
   FILE*f=fopen("dl_sync_slots.u8","ab");
   if(f){fwrite(slot,1,510,f);fclose(f);}
   spin_unlock(&event_spin);
  }
  memcpy(raw,slot+94,120);
  dl_decode_raw(c,d,raw,120,3,2,time_s);
  if(d->have_cell){
   memcpy(raw,slot+282,216);
   dl_decode_raw(c,d,raw,216,d->cell_seed,2,time_s);
  }
  return;
 }

 if(!d->have_cell)return;
 uint8_t aach[30];memcpy(aach,slot+230,14);memcpy(aach+14,slot+266,16);
 int usage=voice_usage(aach,d->cell_seed);
 int frame=d->anchor_fn;int slot_delta=(int)(((long long)abs_start-(long long)d->anchor_abs)/510);
 if(frame>=1&&frame<=18)frame=(((frame-1)+(d->anchor_tn-1+slot_delta)/4)%18+18)%18+1;
 if(n1>=19&&n1>=n2&&n1>=n3){
  d->slot_miss_streak=0;
  d->norm1_slots++;
  memcpy(raw,slot+14,216);
  memcpy(raw+216,slot+282,216);
  if(usage>=4&&frame!=18)voice_slot(raw,d->cell_seed,usage,0,c->frequency,frame,now());
  else dl_decode_raw(c,d,raw,432,d->cell_seed,0,time_s);
 }else if(n2>=19&&n2>=n3){
  d->slot_miss_streak=0;
  d->norm2_slots++;
  d->second_stolen=0;memcpy(raw,slot+14,216);
  dl_decode_raw(c,d,raw,216,d->cell_seed,1,time_s);
  memcpy(raw+216,slot+282,216);
  if(usage>=4&&frame!=18&&!d->second_stolen)voice_slot(raw,d->cell_seed,usage,1,c->frequency,frame,now());
  else dl_decode_raw(c,d,raw+216,216,d->cell_seed,1,time_s);
 }else if(++d->slot_miss_streak>=64){
  d->locked=0;
  d->have_next=0;
  d->next_detect_at=d->total_raw;
  d->slot_miss_streak=0;
 }
}

#include "receiver_diversity.h"

static void dl_slots_ready(Channel*c,DLDecoder*d){
 if(!d->locked||!d->have_next)return;
 uint64_t boundary=d->slot_abs0;
 if(d->have_slot_end&&d->last_slot_end>boundary)boundary=d->last_slot_end;
 d->next_slot_abs=dl_slot_at_or_after(d->next_slot_abs,boundary);
 for(;;){
  uint64_t len=d->total_bits-d->slot_abs0;
  uint64_t rel=d->next_slot_abs-d->slot_abs0;
  if(rel+510>len)break;
  if(diversity)diversity_dl_offer(c,d,d->slot_bits+rel,d->next_slot_abs);
  else dl_process_slot(c,d,d->slot_bits+rel,d->next_slot_abs);
  d->last_slot_end=d->next_slot_abs+510;
  d->have_slot_end=1;
  if(!d->locked)break;
  d->next_slot_abs+=510;
 }
 if(d->next_slot_abs>d->slot_abs0+1020){
  uint64_t drop=d->next_slot_abs-1020-d->slot_abs0;
  uint64_t len=d->total_bits-d->slot_abs0;
  if(drop>len)drop=len;
  if(drop){
   memmove(d->slot_bits,d->slot_bits+drop,len-drop);
   d->slot_abs0+=drop;
  }
 }
}

static void dl_append_slot_pair(Channel*c,DLDecoder*d,uint8_t raw){
 if(!d->locked)return;
 uint64_t len=d->total_bits-d->slot_abs0;
 if(len+2>=DL_BITS){
  uint64_t drop=len-DL_BITS+1024;
  memmove(d->slot_bits,d->slot_bits+drop,len-drop);
  d->slot_abs0+=drop;
  len-=drop;
 }
 uint8_t m=dl_map[d->mapping&7][raw&3];
 d->slot_bits[len]=m>>1;
 d->slot_bits[len+1]=m&1;
 d->total_bits+=2;
 dl_slots_ready(c,d);
}

static void dl_lock(Channel*c,DLDecoder*d,int mapping,uint64_t abs_bit){
 if(abs_bit<214)return;
 int rn=(int)(d->total_raw-d->raw_abs0);
 int nb=rn*2;
 if(nb>DL_BITS)nb=DL_BITS;
 for(int i=0;i<rn&&2*i+1<DL_BITS;i++){
  uint8_t m=dl_map[mapping&7][d->raw[i]&3];
  d->slot_bits[2*i]=m>>1;
  d->slot_bits[2*i+1]=m&1;
 }
 d->mapping=mapping&7;
 d->locked=1;
 d->have_next=1;
 d->slot_phase=(int)((abs_bit-214)%510);
 d->last_sync_abs=abs_bit;
 d->fec_fail_streak=0;
 d->slot_miss_streak=0;
 d->slot_abs0=d->raw_abs0*2;
 d->total_bits=d->slot_abs0+nb;
 d->next_slot_abs=abs_bit-214;
 dl_slots_ready(c,d);
}

static void dl_detect(Channel*c,DLDecoder*d){
 if(d->total_raw<d->next_detect_at)return;
 d->next_detect_at=d->total_raw+3600;
 d->detect_runs++;
 int rn=(int)(d->total_raw-d->raw_abs0);
 if(rn<300)return;
 uint8_t bits[DL_RAW*2];
 int maps=d->locked?1:8;
 for(int mm=0;mm<maps;mm++){
  int mi=d->locked?d->mapping:mm;
  for(int i=0;i<rn;i++){
   uint8_t m=dl_map[mi][d->raw[i]&3];
   bits[2*i]=m>>1;
   bits[2*i+1]=m&1;
  }
  int nb=rn*2,best=0,best_pos=-1;
  dl_find_sync38(bits,nb,dl_sync_pat,&best,&best_pos);
  if(best>d->best_sync){d->best_sync=best;d->best_map=mi;}
  if(best>=35&&best_pos>=120){
   /* Training alone is not acquisition. Validate its preceding BSCH and
    * prefer the newest match so resync never replays a stale raw window. */
   Event probe={0};
   if(!pt_fec(bits+best_pos-120,120,3,&probe))continue;
   uint64_t abs_bit=d->raw_abs0*2+(uint64_t)best_pos;
   if(!d->locked){
    dl_lock(c,d,mi,abs_bit);
   }else{
    uint64_t delta=abs_bit>d->last_sync_abs?abs_bit-d->last_sync_abs:d->last_sync_abs-abs_bit;
    if(delta>=80){
     int phase=(int)((abs_bit-214)%510);
     d->last_sync_abs=abs_bit;
     if(phase==d->slot_phase){
      d->relock_candidate=-1;
      d->relock_hits=0;
     }else if(best>=37){
      if(d->relock_candidate==phase)d->relock_hits++;
      else{d->relock_candidate=phase;d->relock_hits=1;}
      if(d->relock_hits>=1){
       dl_lock(c,d,mi,abs_bit);
       d->relock_candidate=-1;
       d->relock_hits=0;
      }
     }
    }
   }
   return;
  }
 }
}

static void dl_append_raw(Channel*c,DLDecoder*d,uint8_t raw){
 int rn=(int)(d->total_raw-d->raw_abs0);
 if(rn>=DL_RAW){
  int drop=DL_RAW/4;
  memmove(d->raw,d->raw+drop,rn-drop);
  d->raw_abs0+=drop;
  rn-=drop;
 }
 d->raw[rn]=raw&3;
 d->total_raw++;
 dl_append_slot_pair(c,d,raw&3);
 dl_detect(c,d);
}

static void dl_symbol(Channel*c,DLDecoder*d,IQ sym){
 if(!d->have_prev){
  d->prev_sym=sym;
  d->have_prev=1;
  return;
 }
 float im=sym.q*d->prev_sym.i-sym.i*d->prev_sym.q;
 float re=sym.i*d->prev_sym.i+sym.q*d->prev_sym.q;
 d->prev_sym=sym;
 int best=(im<0?2:0)+(re<0?1:0);
 dl_append_raw(c,d,(uint8_t)best);
}

static IQ channel_dynamic_afc(Channel*c,IQ x);
static IQ dl_rrc(DLDecoder*d,IQ x);

static void dl_cont_feed(Channel*c,DLDecoder*d,IQ x){
 if(d->omega<=0){
   d->omega=active_out_fs/18000.0f;
  d->tpos=d->omega;
  d->next_detect_at=3600;
  d->cell_seed=0;
  d->have_cell=0;
 }
 float p=x.i*x.i+x.q*x.q;
 if(d->agc_power<=0){
  d->agc_power=p;
  if(p>1e-9f)d->agc_gain=1.0f/sqrtf(p);
 }
 else d->agc_power=.999f*d->agc_power+.001f*p;
 if((d->agc_count++&63)==0&&d->agc_power>1e-9f)
  d->agc_gain=1.0f/sqrtf(d->agc_power);
 if(d->agc_gain>0){
  x.i*=d->agc_gain;
  x.q*=d->agc_gain;
 }
 /* The eighth-power estimator on oversampled, pulse-shaped pi/4-DQPSK
  * drifts towards modulation components. Fixed DL was measured at >99% CRC
  * without it. Keep manual per-channel offsets; do not steer direct DL with
  * this unqualified estimate. The wideband path retains its existing AFC. */
 if(!direct_dl_mode&&active_rf_fs!=RF_FS&&!fpga_stage2)x=channel_dynamic_afc(c,x);
 x=dl_rrc(d,x);
 if(d->sn<DL_SBUF)d->sbuf[d->sn++]=x;
 else{
  memmove(d->sbuf,d->sbuf+DL_SBUF/4,sizeof(IQ)*(DL_SBUF-DL_SBUF/4));
  d->sn-=DL_SBUF/4;
  d->tpos-=DL_SBUF/4;
  if(d->tpos<0)d->tpos=0;
  d->sbuf[d->sn++]=x;
 }
 for(;;){
  float half=0.5f*d->omega;
  if(d->tpos-half<0||d->tpos+half+1>=d->sn)break;
  IQ early=dl_linsamp(d,d->tpos-half);
  IQ mid=dl_linsamp(d,d->tpos);
  IQ late=dl_linsamp(d,d->tpos+half);
  dl_symbol(c,d,mid);
  float ei=late.i-early.i,eq=late.q-early.q;
  float err=ei*mid.i+eq*mid.q;
  if(err>2)err=2;else if(err<-2)err=-2;
  /* Track oscillator drift, not symbol-to-symbol noise. A 2% clock excursion
   * loses slot alignment; even 1000 ppm allows ample headroom for the SDR. */
  d->omega+=0.00001f*err;
   float nom=active_out_fs/18000.0f;
  if(d->omega<nom*.999f)d->omega=nom*.999f;
  else if(d->omega>nom*1.001f)d->omega=nom*1.001f;
  d->tpos+=d->omega+0.025f*err;
 }
 int keep=(int)floor(d->tpos-d->omega-4);
 if(keep>0&&keep<d->sn){
  memmove(d->sbuf,d->sbuf+keep,sizeof(IQ)*(d->sn-keep));
  d->sn-=keep;
  d->tpos-=keep;
 }
}

static void baseband(Channel*c,IQ x){
 c->ring[c->bbcount%RING]=x;
 c->bbcount++;
 c->power+=x.i*x.i+x.q*x.q;
 int win=(direct_ul_mode||fpga_stage2)?(int)(active_out_fs/1280):125;
 if(++c->wn<win)return;

 float p=(float)(c->power/win);
 c->power=0;
 c->wn=0;

 if(c->warm<200){
  c->initial[c->warm++]=p;
  if(c->warm==200){
   for(int i=1;i<200;i++){
    float v=c->initial[i];
    int j=i;
    while(j&&c->initial[j-1]>v){
     c->initial[j]=c->initial[j-1];
     j--;
    }
    c->initial[j]=v;
   }
   c->floor_power=c->initial[100];
  }
  return;
 }

  if(debug_tetra&&(c==&dl[0]||c==&dl[1])&&c->bbcount>56000&&c->bbcount-c->last_debug>=50000){
  c->last_debug=c->bbcount;
   emit(c,(long long)c->bbcount-56000,(long long)c->bbcount);
 }

 int active=p>c->floor_power*2;
 if(p<c->floor_power*15/8)
  c->floor_power=.999f*c->floor_power+.001f*p;

 if(active){
  if(c->begin<0)c->begin=(long long)c->bbcount-win;
  c->end=(long long)c->bbcount;
  c->gap=0;
 }else if(c->begin>=0){
  int pad=(direct_ul_mode||fpga_stage2)?(int)(active_out_fs*.0046875):750;
  int gap_windows=(pad+win-1)/win;if(gap_windows<6)gap_windows=6;
  if(++c->gap>=gap_windows){
   long long len=c->end-c->begin;
   if(len>=pad&&len<=pad*10)
    emit(c,c->begin-pad,c->end+pad);
   c->begin=-1;
  }
 }

 if(c->begin>=0&&(long long)c->bbcount-c->begin>RING-1500)
  c->begin=-1;
}

static void channel_reset(Channel*c,int frequency){
 c->frequency=frequency;
 /* Queued FEC jobs may still read this immutable channel identity. */
 memset((char*)c+sizeof(c->frequency),0,sizeof(*c)-sizeof(c->frequency));
 c->begin=-1;
 c->dyn_nco_i=1;
 c->dyn_step_i=1;
}

static void channel_set_afc(Channel*c,int hz){
 c->afc_hz=hz;
 c->afc_phase=0;
 c->afc_step=(float)(2*PI*hz/(direct_dl_mode?active_rf_fs:CLUSTER_FS));
}

static IQ cpow8(IQ x){
 IQ y=x;
 for(int k=1;k<8;k++){
  float i=y.i*x.i-y.q*x.q;
  float q=y.i*x.q+y.q*x.i;
  y.i=i;y.q=q;
 }
 return y;
}

static IQ channel_dynamic_afc(Channel*c,IQ x){
 float rms=x.i*x.i+x.q*x.q;
 if((c->dyn_skip++&3)==0&&rms>1e-6f){
  IQ z=cpow8(x);
  if(c->dyn_have){
   c->dyn_cross+=(double)z.q*c->dyn_prev8.i-(double)z.i*c->dyn_prev8.q;
   c->dyn_dot+=(double)z.i*c->dyn_prev8.i+(double)z.q*c->dyn_prev8.q;
    if(++c->dyn_count>=256){
     float est=atan2f((float)c->dyn_cross,(float)c->dyn_dot)*active_out_fs/(2*PI*8*4);
    if(fabsf(est)<2500.0f)
     c->dyn_cfo=.97f*c->dyn_cfo+.03f*est;
    float a=(float)(2*PI*c->dyn_cfo/active_out_fs);
    c->dyn_step_i=cosf(a);
    c->dyn_step_q=sinf(a);
    c->dyn_cross=c->dyn_dot=0;
    c->dyn_count=0;
   }
  }
  c->dyn_prev8=z;
  c->dyn_have=1;
 }

 float co=c->dyn_nco_i,si=c->dyn_nco_q;
 IQ y={x.i*co+x.q*si,x.q*co-x.i*si};
 c->dyn_nco_i=co*c->dyn_step_i-si*c->dyn_step_q;
 c->dyn_nco_q=si*c->dyn_step_i+co*c->dyn_step_q;
 if((c->out_count&4095)==0){
  float n=sqrtf(c->dyn_nco_i*c->dyn_nco_i+c->dyn_nco_q*c->dyn_nco_q);
  if(n>0){c->dyn_nco_i/=n;c->dyn_nco_q/=n;}
 }
 return y;
}

static int cic2_decch_push(CIC2DecCh*d,float i,float q,IQ*out){
 int p=d->pos;

 d->sum1_i += i - d->x_i[p];
 d->sum1_q += q - d->x_q[p];
 d->x_i[p]=i;
 d->x_q[p]=q;

 d->sum2_i += d->sum1_i - d->s1_i[p];
 d->sum2_q += d->sum1_q - d->s1_q[p];
 d->s1_i[p]=d->sum1_i;
 d->s1_q[p]=d->sum1_q;

 d->pos++;
 if(d->pos==CH_DECIM)d->pos=0;

 if(++d->dec<CH_DECIM)return 0;
 d->dec=0;

 out->i=d->sum2_i*(1.0f/(CH_DECIM*CH_DECIM));
 out->q=d->sum2_q*(1.0f/(CH_DECIM*CH_DECIM));
 return 1;
}

static int cic2_dec3_push(CIC2Dec3*d,float i,float q,IQ*out){
 int p=d->pos;
 d->sum1_i+=i-d->x_i[p];
 d->sum1_q+=q-d->x_q[p];
 d->x_i[p]=i;
 d->x_q[p]=q;
 d->sum2_i+=d->sum1_i-d->s1_i[p];
 d->sum2_q+=d->sum1_q-d->s1_q[p];
 d->s1_i[p]=d->sum1_i;
 d->s1_q[p]=d->sum1_q;
 if(++d->pos==3)d->pos=0;
 if(++d->dec<3)return 0;
 d->dec=0;
 out->i=d->sum2_i*(1.0f/9.0f);
 out->q=d->sum2_q*(1.0f/9.0f);
 return 1;
}

static void channel_post_decim(Channel*c,DLDecoder*decoder,IQ y){
 IQ out=y;
 if(!(dl_pair_mode&&decoder)){
 c->hi[c->at]=c->hi[c->at+OUT_TAPS]=y.i;
 c->hq[c->at]=c->hq[c->at+OUT_TAPS]=y.q;
 c->at=(c->at+1)%OUT_TAPS;

 float yi=0,yq=0;
#ifdef __arm__
 float32x4_t ai=vdupq_n_f32(0),aq=vdupq_n_f32(0);
 int j=0;
 for(;j<=OUT_TAPS-4;j+=4){
  float32x4_t h=vld1q_f32(out_taps+j);
  ai=vmlaq_f32(ai,vld1q_f32(c->hi+c->at+j),h);
  aq=vmlaq_f32(aq,vld1q_f32(c->hq+c->at+j),h);
 }
 float ti[4],tq[4];
 vst1q_f32(ti,ai);
 vst1q_f32(tq,aq);
 yi=ti[0]+ti[1]+ti[2]+ti[3];
 yq=tq[0]+tq[1]+tq[2]+tq[3];
 for(;j<OUT_TAPS;j++){
  yi+=c->hi[c->at+j]*out_taps[j];
  yq+=c->hq[c->at+j]*out_taps[j];
 }
#else
 for(int j=0;j<OUT_TAPS;j++){
  yi+=c->hi[c->at+j]*out_taps[j];
  yq+=c->hq[c->at+j]*out_taps[j];
 }
#endif

 out=(IQ){yi,yq};
 }
 c->out_count++;
 __sync_fetch_and_add(&output_samples,1);
 if(decoder)dl_cont_feed(c,decoder,out);
 else baseband(c,out);
}

static void channel_feed_mixed(Channel*c,DLDecoder*decoder,IQ x){
 if(c->afc_hz){
  float co=(float)cos(c->afc_phase),si=(float)sin(c->afc_phase);
  float ii=x.i*co+x.q*si;
  float qq=x.q*co-x.i*si;
  x.i=ii;
  x.q=qq;
  c->afc_phase+=c->afc_step;
  if(c->afc_phase>PI)c->afc_phase-=2*PI;
  else if(c->afc_phase<-PI)c->afc_phase+=2*PI;
 }
 IQ y;
 if(cic2_decch_push(&c->d10,x.i,x.q,&y))
  channel_post_decim(c,decoder,y);
}

static void channel_feed_pair(Channel*c,DLDecoder*decoder,IQ x){
 IQ y;
 if(cic2_dec3_push(&c->d3,x.i,x.q,&y))
  channel_post_decim(c,decoder,y);
}

static int cic2_dec5_push(CIC2Dec5*d,float i,float q,IQ*out){
 int p=d->pos;

 d->sum1_i += i - d->x_i[p];
 d->sum1_q += q - d->x_q[p];
 d->x_i[p]=i;
 d->x_q[p]=q;

 d->sum2_i += d->sum1_i - d->s1_i[p];
 d->sum2_q += d->sum1_q - d->s1_q[p];
 d->s1_i[p]=d->sum1_i;
 d->s1_q[p]=d->sum1_q;

 d->pos++;
 if(d->pos==5)d->pos=0;

 if(++d->dec<5)return 0;
 d->dec=0;

 out->i=d->sum2_i*(1.0f/25.0f);
 out->q=d->sum2_q*(1.0f/25.0f);
 return 1;
}

static void split3(IQ x,uint32_t phase,IQ*out_lo,IQ*out_mid,IQ*out_hi){
 float c=side_c[phase],s=side_s[phase];
 float ic=x.i*c, qs=x.q*s, is=x.i*s, qc=x.q*c;

 out_lo->i=ic-qs;
 out_lo->q=is+qc;

 *out_mid=x;

 out_hi->i=ic+qs;
 out_hi->q=-is+qc;
}

static void probe_feed(PowerProbe*p,IQ x){
 float pw=x.i*x.i+x.q*x.q;
 p->power=.9995f*p->power+.0005f*pw;
}

static void maybe_select_dl(void){
 if(++dl_probe_samples<250000)return;
 dl_probe_samples=0;
}

#define PAIR_BLOCK 262144
#define PAIR_CLUSTER_CAP ((PAIR_BLOCK+4)/5+2)
static IQ pair_bb[PAIR_CLUSTER_CAP];
static size_t pair_count;
static uint64_t pair_base;
static volatile int pair_job;

static void pair_process_channel(int index){
 int lower=index==0;
 for(size_t k=0;k<pair_count;k++){
  uint32_t p=(uint32_t)(((pair_base+k)*5)&31);
  float c=pair_c[p],s=pair_s[p];
  float i=pair_bb[k].i,q=pair_bb[k].q;
  float ic=i*c,qs=q*s,is=i*s,qc=q*c;
  IQ x=lower?(IQ){ic-qs,is+qc}:(IQ){ic+qs,qc-is};
  if(((pair_base+k)&15)==0)probe_feed(&dp[index],x);
  channel_feed_pair(&dl[index],&dl_cont[index],x);
 }
}

static void*pair_worker(void*unused){
 (void)unused;
 for(;;){
  while(pair_job==0||pair_job==2)sched_yield();
  __sync_synchronize();
  if(pair_job==3)break;
  pair_process_channel(0);
  __sync_synchronize();
  pair_job=2;
 }
 return 0;
}

static void feed(short i,short q){
 if(dl_pair_mode){
  uint32_t p=(uint32_t)(input_samples&31);
  float c=pair_c[p],s=pair_s[p];
  float ic=i*c,qs=q*s,is=i*s,qc=q*c;
  IQ lower={ic-qs,is+qc};
  IQ upper={ic+qs,qc-is};
  input_samples++;
  if(dl_enabled){
   probe_feed(&dp[0],lower);
   probe_feed(&dp[1],upper);
   channel_feed_mixed(&dl[0],&dl_cont[0],lower);
   channel_feed_mixed(&dl[1],&dl_cont[1],upper);
   cluster_samples++;
  }
  return;
 }
 if(direct_dl_mode){
  input_samples++;
  if(dl_enabled){
   IQ x={(float)i,(float)q};
   probe_feed(&dp[1],x);
   channel_feed_mixed(&dl[1],&dl_cont[1],x);
   cluster_samples++;
  }
  return;
 }
 uint32_t p=(uint32_t)(input_samples%12);
 float c=cluster_c[p],s=cluster_s[p];

 float ic=i*c;
 float qs=q*s;
 float is=i*s;
 float qc=q*c;

 float uli=ic-qs, ulq=is+qc;
 float dli=ic+qs, dlq=-is+qc;

 input_samples++;

 IQ u,d;
 int ru=cic2_dec5_push(&ul_dec,uli,ulq,&u);
 int rd=dl_enabled?cic2_dec5_push(&dl_dec,dli,dlq,&d):0;

 if(ru){
  channel_feed_mixed(&ul[0],0,u);
  cluster_samples++;
 }

 if(rd){
  probe_feed(&dp[1],d);
  channel_feed_mixed(&dl[1],&dl_cont[1],d);
 }
}

static void make_lpf(float*t,int n,double cutoff,double fs){
 double sum=0;
 for(int j=0;j<n;j++){
  int m=j-(n-1)/2;
  double a=2*PI*cutoff/fs;
  double h=m?sin(a*m)/(PI*m):2*cutoff/fs;
  double w=.42-.5*cos(2*PI*j/(n-1))+.08*cos(4*PI*j/(n-1));
  t[j]=(float)(h*w);
  sum+=t[j];
 }
 for(int j=0;j<n;j++)t[j]/=(float)sum;
}

static void init_rrc(void){
 float alpha=.35f,sps=active_out_fs/18000.0f;
 float energy=0;
 for(int i=0;i<DL_RRC_TAPS;i++){
  float t=(i-(DL_RRC_TAPS-1)*0.5f)/sps;
  float h;
  if(fabsf(t)<1e-6f){
   h=1.0f+alpha*(4.0f/PI-1.0f);
  }else if(fabsf(fabsf(t)-1.0f/(4.0f*alpha))<1e-4f){
   h=(alpha/sqrtf(2.0f))*((1.0f+2.0f/PI)*sinf(PI/(4.0f*alpha))+
      (1.0f-2.0f/PI)*cosf(PI/(4.0f*alpha)));
  }else{
   float den=PI*t*(1.0f-(4.0f*alpha*t)*(4.0f*alpha*t));
   h=(sinf(PI*t*(1.0f-alpha))+4.0f*alpha*t*cosf(PI*t*(1.0f+alpha)))/den;
  }
  rrc_taps[i]=h;
  energy+=h*h;
 }
 if(energy>0){
  float g=1.0f/sqrtf(energy);
  for(int i=0;i<DL_RRC_TAPS;i++)rrc_taps[i]*=g;
 }
}

static IQ dl_rrc(DLDecoder*d,IQ x){
 d->rrc_i[d->rrc_pos]=d->rrc_i[d->rrc_pos+DL_RRC_TAPS]=x.i;
 d->rrc_q[d->rrc_pos]=d->rrc_q[d->rrc_pos+DL_RRC_TAPS]=x.q;
 d->rrc_pos=(d->rrc_pos+1)%DL_RRC_TAPS;
 if(d->rrc_seen<DL_RRC_TAPS)d->rrc_seen++;
 IQ y={0,0};
#ifdef __arm__
 float32x4_t ai=vdupq_n_f32(0),aq=vdupq_n_f32(0);
 int j=0;
 for(;j<=DL_RRC_TAPS-4;j+=4){
  float32x4_t h=vld1q_f32(rrc_taps+j);
  ai=vmlaq_f32(ai,vld1q_f32(d->rrc_i+d->rrc_pos+j),h);
  aq=vmlaq_f32(aq,vld1q_f32(d->rrc_q+d->rrc_pos+j),h);
 }
 float ti[4],tq[4];
 vst1q_f32(ti,ai);vst1q_f32(tq,aq);
 y.i=ti[0]+ti[1]+ti[2]+ti[3];
 y.q=tq[0]+tq[1]+tq[2]+tq[3];
 for(;j<DL_RRC_TAPS;j++){
  y.i+=d->rrc_i[d->rrc_pos+j]*rrc_taps[j];
  y.q+=d->rrc_q[d->rrc_pos+j]*rrc_taps[j];
 }
#else
 for(int j=0;j<DL_RRC_TAPS;j++){
  y.i+=d->rrc_i[d->rrc_pos+j]*rrc_taps[j];
  y.q+=d->rrc_q[d->rrc_pos+j]*rrc_taps[j];
 }
#endif
 return y;
}

static void init_tables(void){
 for(int k=0;k<12;k++){
  double a=2*PI*(5000000.0/RF_FS)*k; /* +/-5 MHz at 12 MS/s */
  cluster_c[k]=(float)cos(a);
  cluster_s[k]=(float)sin(a);
 }
 for(int k=0;k<100;k++){
  double a=2*PI*(425000.0/CLUSTER_FS)*k;
  side_c[k]=(float)cos(a);
  side_s[k]=(float)sin(a);
 }
 for(int k=0;k<32;k++){
  double a=2*PI*k/32.0;
  pair_c[k]=(float)cos(a);
  pair_s[k]=(float)sin(a);
 }
}

static void div_process_channel(int b){
 int ch=b?0:1;
 for(size_t k=0;k<div_count;k++){
  div_capture_sample[b]=div_base+k+1;
  IQ x={div_i[b][k],div_q[b][k]};probe_feed(&dp[ch],x);
  channel_feed_mixed(&dl[ch],&dl_cont[ch],x);
 }
}
static void*div_worker(void*unused){
 (void)unused;
 for(;;){while(!div_job)sched_yield();__sync_synchronize();if(div_job==3)break;
  div_process_channel(1);__sync_synchronize();div_job=2;while(div_job==2)sched_yield();
 }
 return 0;
}

static int parse_scan_channels(const char*text){
 int count=0;
 const char*p=text;
 while(p&&*p&&count<MAX_SCAN_CHANNELS){
  int value=0,digits=0;
  while(*p>='0'&&*p<='9'){value=value*10+(*p-'0');p++;digits++;}
  if(!digits||value<70000000||value>1000000000)return 0;
  scan_candidates[count++]=value;
  if(!*p)break;
  if(*p++!=',')return 0;
 }
 if(!count||*p)return 0;
 scan_count=count;
 return 1;
}

static int scan_retune(struct iio_channel*osc,int frequency){
 if(iio_channel_attr_write_longlong(osc,"frequency",frequency)<0)return 0;
 active_lo_hz=frequency;
 dl_freq[1]=frequency;
 if(diversity){diversity_flush(1);div_expected_seed=0;dl_freq[0]=frequency;memset(&dl_cont[0],0,sizeof(dl_cont[0]));memset(&dp[0],0,sizeof(dp[0]));channel_reset(&dl[0],frequency);}

 memset(&dl_cont[1],0,sizeof(dl_cont[1]));
 memset(&dp[1],0,sizeof(dp[1]));
 channel_reset(&dl[1],frequency);
 channel_set_afc(&dl[1],dl_afc_hz[1]);
 scan_tuned_at=now();
 return 1;
}

static void scan_clear_discovery(void){
 memset(scan_discovered_dl,0,sizeof(scan_discovered_dl));
 memset(scan_discovered_ul,0,sizeof(scan_discovered_ul));
 memset(scan_candidate_crc,0,sizeof(scan_candidate_crc));
 memset(scan_candidate_power,0,sizeof(scan_candidate_power));
}

static int scan_best_candidate(void){
 int best=-1;
  for(int i=0;i<scan_count;i++){
   if(!scan_discovered_dl[i])continue;
   if(!dl_scan_matches_target(scan_target_ul_hz,scan_discovered_ul[i]))continue;
   if(best<0||scan_candidate_crc[i]>scan_candidate_crc[best]||
      (scan_candidate_crc[i]==scan_candidate_crc[best]&&scan_candidate_power[i]>scan_candidate_power[best]))
    best=i;
  }
 return best;
}

/* Bounded capture in the isolated /run test directory for offline UL validation. */
static void fpga_capture_ul(short i,short q){
 static FILE*f;static unsigned int count;static short block[16384];static int used;
 const unsigned int limit=273224u*45u;
 if(count>=limit)return;
 if(count++==0)f=fopen("ul-capture.sc16","wb");
 if(!f)return;
 block[used++]=i;block[used++]=q;
 if(used==16384){fwrite(block,sizeof(short),used,f);used=0;}
 if(count==limit){if(used)fwrite(block,sizeof(short),used,f);fclose(f);f=0;}
}
static void fpga_stream_flush(void){
 if(!ul_stream_staged)return;
 unsigned int w=__atomic_load_n(&ul_stream_write,__ATOMIC_RELAXED);
 ul_stream_queue[w%UL_STREAM_QUEUE].count=ul_stream_staged;
 ul_stream_staged=0;__atomic_store_n(&ul_stream_write,w+1,__ATOMIC_RELEASE);
}
static void fpga_stream_push(void){
 if(fpga_stream_gap&&ul_stream_staged)fpga_stream_flush();
 unsigned int w=__atomic_load_n(&ul_stream_write,__ATOMIC_RELAXED),r=__atomic_load_n(&ul_stream_read,__ATOMIC_ACQUIRE);
 if(w-r>=UL_STREAM_QUEUE){ul_stream_dropped++;fpga_stream_gap=1;return;}
 ULStreamBlock*b=&ul_stream_queue[w%UL_STREAM_QUEUE];
 if(!ul_stream_staged){b->reset=fpga_stream_gap;fpga_stream_gap=0;}
 memcpy(b->iq[ul_stream_staged],fpga_stream_iq,ul_count*sizeof(fpga_stream_iq[0]));
 if(++ul_stream_staged==UL_STREAM_BLOCK)fpga_stream_flush();
}
static void*fpga_stream_worker(void*unused){
 (void)unused;
 for(;;){
  unsigned int r=__atomic_load_n(&ul_stream_read,__ATOMIC_RELAXED),w=__atomic_load_n(&ul_stream_write,__ATOMIC_ACQUIRE);
  if(r==w){if(__atomic_load_n(&ul_stream_stopping,__ATOMIC_ACQUIRE))break;usleep(1000);continue;}
  ULStreamBlock*b=&ul_stream_queue[r%UL_STREAM_QUEUE];
  if(b->reset)for(int j=0;j<ul_count;j++)channel_reset(&ul[j*2],ul_freq[j]);
  for(int i=0;i<b->count;i++)for(int j=0;j<ul_count;j++)channel_post_decim(&ul[j*2],0,(IQ){b->iq[i][j][0],b->iq[i][j][1]});
  ul_stream_completed+=b->count;
  __atomic_store_n(&ul_stream_read,r+1,__ATOMIC_RELEASE);
 }
 return 0;
}
static void fpga_consume(short i,short q,unsigned short m0,unsigned short m1){
 static int next_slot=0,seq=-1,last_seq=-1,last_aux=-1,frame_aux=-1;
 fpga_records++;
 if((m0&0xff80)!=0xa700||(m1&0xff00)!=0x5c00){fpga_meta_errors++;fpga_stream_gap=1;next_slot=0;seq=-1;return;}
 int slot=m0&15,aux=(m0>>4)&7,current=m1&255;
 if(slot>=ul_count+2||aux>=ul_count){fpga_meta_errors++;fpga_stream_gap=1;seq=-1;return;}
 if(seq<0&&last_seq<0&&slot!=0)return; /* DMA may start between complete frames. */
 if(slot==0){if(last_seq>=0&&((last_seq+1)&255)!=current){fpga_seq_errors++;fpga_stream_gap=1;}next_slot=0;seq=current;frame_aux=aux;}
 if(seq<0||slot!=next_slot||current!=seq||aux!=frame_aux){fpga_meta_errors++;fpga_stream_gap=1;seq=-1;return;}
 fpga_slot_samples[slot]++;next_slot++;
 if(slot<ul_count){fpga_stream_iq[slot][0]=i;fpga_stream_iq[slot][1]=q;}
 if(slot==ul_count)channel_post_decim(&dl[1],&dl_cont[1],(IQ){i,q});
 if(slot==ul_count+1){
  if(aux!=last_aux){dl_freq[0]=ul_freq[aux]+10000000;memset(&dl_cont[0],0,sizeof(dl_cont[0]));channel_reset(&dl[0],dl_freq[0]);multi_aux_done=0;last_aux=aux;}
  if(!multi_aux_done)channel_post_decim(&dl[0],&dl_cont[0],(IQ){i,q});
  fpga_stream_push();fpga_frames++;last_seq=seq;seq=-1;next_slot=0;
 }
}
extern int open(const char*,int,...);
extern int close(int);
extern void*mmap(void*,size_t,int,int,int,long);
extern int munmap(void*,size_t);
static int configure_profile_fpga(void){
 uint32_t words[4]={0,0,0,0};
 for(int j=0;j<ul_count+1;j++){
  int f=j<ul_count?ul_freq[j]:dl_freq[1];
  int phase=((active_lo_hz-f)/25000)%500;if(phase<0)phase+=500;
  int bit=j<8?j*9:72;if(j==ul_count)bit=72;
  for(int b=0;b<9;b++)if(phase&(1<<b))words[(bit+b)/32]|=1u<<((bit+b)%32);
 }
 words[2]|=(uint32_t)ul_count<<17;
 for(int j=0;j<ul_count;j++)if(ul_freq[j]+10000000==dl_freq[1])words[2]|=(uint32_t)j<<22;
 int fd=open("/dev/mem",2|0x101000);if(fd<0)return 0;
 volatile uint32_t*g[2];
 for(int j=0;j<2;j++){
  g[j]=mmap(0,4096,3,1,fd,0x43c00000+j*0x10000);
  if((void*)g[j]==(void*)-1){if(j)munmap((void*)g[0],4096);close(fd);return 0;}
 }
 g[1][0]=0;__sync_synchronize();usleep(1000);
 g[0][0]=words[0];g[0][2]=words[1];g[1][0]=words[2];g[1][2]=words[3];__sync_synchronize();usleep(1000);
 words[2]|=1u<<21;g[1][0]=words[2];__sync_synchronize();
 int ok=g[0][0]==words[0]&&g[0][2]==words[1]&&g[1][0]==words[2];
 munmap((void*)g[0],4096);munmap((void*)g[1],4096);close(fd);return ok;
}

int main(int argc,char**argv){
 int seconds=86400;
 int preferred_lo=0,requested_ul=0,requested_dl=0;
 double gain=40;

 for(int j=1;j<argc;j++){
  if(!strcmp(argv[j],"--seconds")&&j+1<argc)seconds=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--ul-hz")&&j+1<argc)requested_ul=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--dl-hz")&&j+1<argc)requested_dl=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--gain")&&j+1<argc)gain=atof(argv[++j]);
  else if(!strcmp(argv[j],"--fpga-stage2"))fpga_stage2=1;
  else if(!strcmp(argv[j],"--fpga-web")){fpga_stage2=1;fpga_web=1;}
  else if(!strcmp(argv[j],"--fpga-dual")){fpga_stage2=1;fpga_web=1;fpga_persistent=1;}
  else if(!strcmp(argv[j],"--capture-bulk"))capture_only=2;
  else if(!strcmp(argv[j],"--capture-only"))capture_only=1;
  else if(!strcmp(argv[j],"--diversity"))diversity=1;
  else if(!strcmp(argv[j],"--direct-ul"))direct_ul_mode=1;
  else if(!strcmp(argv[j],"--rx")&&j+1<argc)rx_port=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--lo")&&j+1<argc)preferred_lo=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--channels")&&j+1<argc){if(!parse_scan_channels(argv[++j]))return 2;}
  else if(!strcmp(argv[j],"--scrambler")&&j+1<argc)seed=strtoul(argv[++j],0,0);
  else if(!strcmp(argv[j],"--debug-tetra"))debug_tetra=1;
  else if(!strcmp(argv[j],"--dl"))dl_enabled=1;
  else if(!strcmp(argv[j],"--direct-dl")){direct_dl_mode=1;dl_enabled=1;}
  else if(!strcmp(argv[j],"--dl-scan")){scan_dl_mode=1;direct_dl_mode=1;dl_enabled=1;}
  else if(!strcmp(argv[j],"--dl-pair")){dl_pair_mode=1;dl_enabled=1;}
  else if(!strcmp(argv[j],"--capture-dl-slots"))capture_dl_slots=1;
  else if(!strcmp(argv[j],"--dl-afc0")&&j+1<argc)dl_afc_hz[0]=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--dl-afc1")&&j+1<argc)dl_afc_hz[1]=atoi(argv[++j]);
  else if(!strcmp(argv[j],"--dl-afc2")&&j+1<argc){j++;}
  else if(!strcmp(argv[j],"--target-ul")&&j+1<argc)scan_target_ul_hz=atoi(argv[++j]);
  else{
   printf("Unknown/incomplete argument: %s\n",argv[j]);
   return 2;
  }
 }
 (void)preferred_lo;(void)requested_ul;(void)requested_dl;if(!fpga_stage2)return 2;

 if(fpga_stage2){
  if(scan_count<1||scan_count>8||requested_dl-requested_ul!=10000000)return 2;
  active_selected_ul=requested_ul;
  ul_count=scan_count;int low=2000000000,high=0;
  for(int i=0;i<ul_count;i++){
   ul_freq[i]=scan_candidates[i];if(ul_freq[i]%25000)return 2;
   for(int j=0;j<i;j++)if(ul_freq[j]==ul_freq[i])return 2;
   if(ul_freq[i]<low)low=ul_freq[i];if(ul_freq[i]+10000000>high)high=ul_freq[i]+10000000;
  }
  int paired=0;for(int i=0;i<ul_count;i++)if(ul_freq[i]+10000000==requested_dl)paired=1;
  if(!paired||high-low>11800000)return 2;
  dl_freq[0]=ul_freq[0]+10000000;dl_freq[1]=requested_dl;active_rf_fs=12500000;active_out_fs=125000;active_rf_bw=12000000;active_lo_hz=((low+high)/2/25000)*25000;
  if(preferred_lo>0&&preferred_lo%25000==0&&low>=preferred_lo-6100000&&high<=preferred_lo+6100000)active_lo_hz=preferred_lo;
  dl_enabled=1;mode="live-ul-dl-fpga";fpga_persistent=1;fpga_web=1;
 }else if(direct_ul_mode){
  ul_count=scan_count?scan_count:1;if(ul_count!=1)return 2;
  for(int i=0;i<ul_count;i++)if(scan_count)ul_freq[i]=scan_candidates[i];
  active_rf_fs=ul_count==1?1200000:2400000;active_out_fs=active_rf_fs/(float)CH_DECIM;active_lo_hz=(ul_freq[0]+ul_freq[ul_count-1])/2;
  int width=ul_freq[ul_count-1]-ul_freq[0];if(width<0||width>1800000)return 2;
  active_rf_bw=width+200000;mode="live-ul-diversity-capable-v1";
  for(int i=0;i<ul_count;i++){double a=-2*PI*(ul_freq[i]-active_lo_hz)/active_rf_fs;ul_ci[i]=cos(a);ul_cq[i]=sin(a);}
 }else if(dl_pair_mode){
  active_rf_fs=PAIR_RF_FS;
  active_rf_bw=400000;
  active_lo_hz=422900000;
  active_out_fs=(float)PAIR_RF_FS/15.0f;
  mode="live-dl-pair-v03";
 }else if(direct_dl_mode){
   if(!scan_dl_mode&&scan_count)dl_freq[1]=scan_candidates[0];
   if(scan_dl_mode){
    if(!scan_count){scan_candidates[0]=422825000;scan_count=1;}
    scan_index=0;
    dl_freq[1]=scan_candidates[0];
   }
  /* A single 25 kHz DL does not need the wideband 2.4 MS/s capture. Leave
   * enough CPU headroom for acquisition and FEC without dropping DMA blocks. */
  active_rf_fs=1200000;
  active_out_fs=(float)active_rf_fs/CH_DECIM;
  active_rf_bw=200000;
   active_lo_hz=dl_freq[1];
   mode=scan_dl_mode?"live-dl-scan-follow-v01":"live-direct-dl-v01";
 }

 if(diversity&&(!direct_dl_mode&&!direct_ul_mode))return 2;
 if(seconds<1||(!fpga_persistent&&seconds>86400)||gain<0||gain>73||rx_port<1||rx_port>2||
    (scan_target_ul_hz&& (scan_target_ul_hz<70000000||scan_target_ul_hz>1000000000)))
  return 2;

 started=now();
 signal(2,(void*)stop);
 signal(15,(void*)stop);

 init_tables();
 make_lpf(out_taps,OUT_TAPS,(active_out_fs<150000||fpga_stage2)?16000:30000,active_out_fs);
 init_rrc();

 for(int i=0;i<ul_count;i++)for(int b=0;b<2;b++)channel_reset(&ul[i*2+b],ul_freq[i]);
 channel_reset(&dl[0],dl_freq[0]);
 channel_reset(&dl[1],dl_freq[1]);
 if(diversity&&direct_dl_mode){dl_freq[0]=dl_freq[1];channel_reset(&dl[0],dl_freq[0]);}
 channel_set_afc(&dl[0],dl_afc_hz[0]);
 channel_set_afc(&dl[1],dl_afc_hz[1]);

 arrays();
 status("starting");

 struct iio_context*ctx=iio_create_local_context();
 if(!ctx){status("iio_context_failed");return 1;}
 iio_context_set_timeout(ctx,3000);

 struct iio_device*phy=iio_context_find_device(ctx,"ad9361-phy");
 struct iio_device*rx=iio_context_find_device(ctx,"cf-ad9361-lpc");
 if(!phy||!rx){status("iio_device_failed");iio_context_destroy(ctx);return 1;}

 char iname[16],qname[16];
 snprintf(iname,sizeof(iname),"voltage%d",diversity?0:(rx_port-1)*2);
 snprintf(qname,sizeof(qname),"voltage%d",diversity?1:(rx_port-1)*2+1);

 struct iio_channel*rf=iio_device_find_channel(phy,"voltage0",0);
 struct iio_channel*osc=iio_device_find_channel(phy,"altvoltage0",1);
 struct iio_channel*ci=iio_device_find_channel(rx,iname,0);
 struct iio_channel*cq=iio_device_find_channel(rx,qname,0);

 struct iio_channel*ci2=0,*cq2=0,*rf2=0;
 if(diversity||fpga_stage2){ci2=iio_device_find_channel(rx,"voltage2",0);cq2=iio_device_find_channel(rx,"voltage3",0);rf2=iio_device_find_channel(phy,"voltage1",0);}
 if(!rf||!osc||!ci||!cq||((diversity||fpga_stage2)&&(!ci2||!cq2||!rf2))){
  status("iio_channel_failed");
  iio_context_destroy(ctx);
  return 1;
 }

 if(iio_channel_attr_write_longlong(rf,"sampling_frequency",active_rf_fs)<0||
    iio_channel_attr_write_longlong(rf,"rf_bandwidth",active_rf_bw)<0||
    iio_channel_attr_write_longlong(osc,"frequency",active_lo_hz)<0||
    iio_channel_attr_write(rf,"gain_control_mode","manual")<0||
    iio_channel_attr_write_double(rf,"hardwaregain",gain)<0){
  status("iio_config_failed");
  iio_context_destroy(ctx);
  return 1;
 }

 if(fpga_stage2){
  long long actual_fs=0,actual_lo=0;
  int fs_rc=iio_channel_attr_read_longlong(rf,"sampling_frequency",&actual_fs);
  int lo_rc=iio_channel_attr_read_longlong(osc,"frequency",&actual_lo);
  printf("FPGA RF readback fs=%lld lo=%lld expected_fs=%d expected_lo=%d\n",actual_fs,actual_lo,active_rf_fs,active_lo_hz);
  if(fs_rc<0||lo_rc<0||(actual_fs-active_rf_fs>10||active_rf_fs-actual_fs>10)||(actual_lo-active_lo_hz>5||active_lo_hz-actual_lo>5)){status("fpga_clock_mismatch");iio_context_destroy(ctx);return 1;}
  unsigned long pl_clock=0;FILE*cf=fopen("/sys/kernel/debug/clk/spi0.0-pl_intf_clk/clk_rate","r");if(cf){fscanf(cf,"%lu",&pl_clock);fclose(cf);}
  printf("FPGA fixed-rate contract: interface_clock=%lu Hz, maximum=100000000 Hz\n",pl_clock);
  if(!pl_clock||pl_clock>100000000){status("fpga_interface_clock_unsupported");iio_context_destroy(ctx);return 1;}
  if(!configure_profile_fpga()){status("fpga_profile_config_failed");iio_context_destroy(ctx);return 1;}

 }
 for(unsigned int j=0;j<iio_device_get_channels_count(rx);j++)
  iio_channel_disable(iio_device_get_channel(rx,j));

 if(diversity&&(iio_channel_attr_write(rf2,"gain_control_mode","manual")<0||iio_channel_attr_write_double(rf2,"hardwaregain",gain)<0)){
  status("diversity_rx2_config_failed");iio_context_destroy(ctx);return 1;
 }
 iio_channel_enable(ci);
 iio_channel_enable(cq);
 if(diversity||fpga_stage2){iio_channel_enable(ci2);iio_channel_enable(cq2);}
 iio_device_set_kernel_buffers_count(rx,fpga_stage2?32:8);

 const struct data_format*fi=iio_channel_get_data_format(ci);
 const struct data_format*fq=iio_channel_get_data_format(cq);

 int fast=fi&&fq&&fi->length==16&&fq->length==16&&
  fi->is_signed&&fq->is_signed&&!fi->is_be&&!fq->is_be&&
  !fi->with_scale&&!fq->with_scale&&fi->repeat==1&&fq->repeat==1&&
  fi->bits>0&&fq->bits>0&&fi->bits+fi->shift<=16&&fq->bits+fq->shift<=16;

 const struct data_format*fi2=diversity?iio_channel_get_data_format(ci2):0,*fq2=diversity?iio_channel_get_data_format(cq2):0;
 int fast2=diversity&&fi2&&fq2&&fi2->length==16&&fq2->length==16&&fi2->is_signed&&fq2->is_signed&&!fi2->is_be&&!fq2->is_be&&!fi2->with_scale&&!fq2->with_scale&&fi2->repeat==1&&fq2->repeat==1&&fi2->bits>0&&fq2->bits>0&&fi2->bits+fi2->shift<=16&&fq2->bits+fq2->shift<=16;
 int checked=0,checked2=0;
 printf("TETRA %s fast=%d fs=%d lo=%d bits=%u/%u shift=%u/%u\n",
  dl_pair_mode?"dual-DL-threaded":(direct_dl_mode?"direct-DL":"wideband-V06"),
  fast,active_rf_fs,active_lo_hz,
  fi?fi->bits:0,fq?fq->bits:0,fi?fi->shift:0,fq?fq->shift:0);

 size_t iio_block=fpga_stage2?65536:(dl_pair_mode||active_rf_fs==RF_FS)?PAIR_BLOCK:(direct_ul_mode?65536:8192);
 struct iio_buffer*buf=iio_device_create_buffer(rx,iio_block,0);
 if(!buf){
  status("iio_buffer_failed_busy");
  iio_context_destroy(ctx);
  return 1;
 }

 double last=0;
 pthread_t pair_thread=0;
 int pair_thread_started=0;
 if(dl_pair_mode){
  if(pthread_create(&pair_thread,0,pair_worker,0)!=0){
   status("pair_thread_failed");
   iio_buffer_destroy(buf);
   iio_context_destroy(ctx);
   return 1;
  }
  pair_thread_started=1;
 }
 pthread_t div_thread=0;int div_thread_started=0;
 if(diversity&&direct_dl_mode){if(pthread_create(&div_thread,0,div_worker,0)!=0){status("diversity_thread_failed");iio_buffer_destroy(buf);iio_context_destroy(ctx);return 1;}div_thread_started=1;}
 pthread_t fpga_stream_thread=0;int fpga_stream_thread_started=0;
 pthread_t fpga_ul_thread=0;
 if(fpga_web){
  if(pthread_create(&fpga_ul_thread,0,fpga_ul_worker,0)!=0){status("ul_worker_failed");iio_buffer_destroy(buf);iio_context_destroy(ctx);return 1;}
  fpga_ul_thread_started=1;
  if(pthread_create(&fpga_stream_thread,0,fpga_stream_worker,0)!=0){
   __atomic_store_n(&fpga_ul_stopping,1,__ATOMIC_RELEASE);pthread_join(fpga_ul_thread,0);
   status("ul_stream_worker_failed");iio_buffer_destroy(buf);iio_context_destroy(ctx);return 1;
  }
  fpga_stream_thread_started=1;
 }
 status("receiving");
 if(scan_dl_mode)scan_tuned_at=now();

 while(running&&now()-started<seconds){
  ssize_t got=iio_buffer_refill(buf);
  if(got<0){failures++;break;}

  char*pi=iio_buffer_first(buf,ci);
  char*pq=iio_buffer_first(buf,cq);
  char*pi2=(diversity||fpga_stage2)?iio_buffer_first(buf,ci2):0,*pq2=(diversity||fpga_stage2)?iio_buffer_first(buf,cq2):0;
  ptrdiff_t step=iio_buffer_step(buf);

  if(!pi||!pq||((diversity||fpga_stage2)&&(!pi2||!pq2))||step<=0||got<=0){failures++;break;}

   size_t scans=(size_t)got/(size_t)step;
   if(diversity&&direct_dl_mode&&scans>DIV_BLOCK){failures++;break;}
   if(dl_pair_mode&&scans>PAIR_BLOCK){failures++;break;}
   size_t pair_out=0;

  if(capture_only==2){
   if(step!=4||pq!=pi+2){failures++;break;}
   size_t copy=input_samples<CAPTURE_SCANS?CAPTURE_SCANS-input_samples:0;if(copy>scans)copy=scans;
   if(copy)memcpy(capture_iq+input_samples*2,pi,copy*4);
   input_samples+=scans;
  }else{
  if(diversity&&direct_dl_mode)div_base=input_samples;
  for(size_t k=0;k<scans;k++){
   char*sip=pi+k*step,*sqp=pq+k*step;
   if(fpga_stage2){fpga_consume(*(short*)sip,*(short*)sqp,*(unsigned short*)(pi2+k*step),*(unsigned short*)(pq2+k*step));input_samples++;continue;}
   short i,q;

   if(fast){
    i=((short)(*(unsigned short*)sip<<(16-fi->bits-fi->shift)))>>(16-fi->bits);
    q=((short)(*(unsigned short*)sqp<<(16-fq->bits-fq->shift)))>>(16-fq->bits);

    if(checked<128){
     short ri,rq;
     iio_channel_convert(ci,&ri,sip);
     iio_channel_convert(cq,&rq,sqp);
     if(ri!=i||rq!=q){fast=0;i=ri;q=rq;}
     checked++;
    }
   }else{
    iio_channel_convert(ci,&i,sip);
    iio_channel_convert(cq,&q,sqp);
   }

    if(capture_only){if(input_samples<CAPTURE_SCANS){capture_iq[input_samples*2]=i;capture_iq[input_samples*2+1]=q;}input_samples++;continue;}
    if(diversity||direct_ul_mode){
     short i2=0,q2=0;
     if(diversity){
      char*si2=pi2+k*step,*sq2=pq2+k*step;
      if(fast2){i2=((short)(*(unsigned short*)si2<<(16-fi2->bits-fi2->shift)))>>(16-fi2->bits);q2=((short)(*(unsigned short*)sq2<<(16-fq2->bits-fq2->shift)))>>(16-fq2->bits);
       if(checked2<128){short ri,rq;iio_channel_convert(ci2,&ri,si2);iio_channel_convert(cq2,&rq,sq2);if(ri!=i2||rq!=q2){fast2=0;i2=ri;q2=rq;}checked2++;}
      }else{iio_channel_convert(ci2,&i2,si2);iio_channel_convert(cq2,&q2,sq2);}
     }
     input_samples++;
     if((input_samples&31)==0){div_power[0]=.999*div_power[0]+.001*((double)i*i+(double)q*q);div_power[1]=.999*div_power[1]+.001*((double)i2*i2+(double)q2*q2);}
     if(direct_ul_mode){
      for(int ch=0;ch<ul_count;ch++){
       float co=ul_ni[ch],si=ul_nq[ch];
       channel_feed_mixed(&ul[ch*2],0,(IQ){i*co-q*si,i*si+q*co});
       if(diversity)channel_feed_mixed(&ul[ch*2+1],0,(IQ){i2*co-q2*si,i2*si+q2*co});
       ul_ni[ch]=co*ul_ci[ch]-si*ul_cq[ch];ul_nq[ch]=co*ul_cq[ch]+si*ul_ci[ch];
       if((input_samples&4095)==0){float norm=1/sqrtf(ul_ni[ch]*ul_ni[ch]+ul_nq[ch]*ul_nq[ch]);ul_ni[ch]*=norm;ul_nq[ch]*=norm;}
      }
     }else{
      div_i[0][k]=i;div_q[0][k]=q;div_i[1][k]=i2;div_q[1][k]=q2;
     }
     cluster_samples++;
    }else if(dl_pair_mode){
     IQ bb;
     if(cic2_dec5_push(&pair_front_dec,(float)i,(float)q,&bb)){
      if(pair_out>=PAIR_CLUSTER_CAP){failures++;break;}
      pair_bb[pair_out++]=bb;
     }
    }else feed(i,q);
  }

  }
   if(dl_pair_mode){
    if(failures)break;
    pair_count=pair_out;
    pair_base=cluster_samples;
    __sync_synchronize();
    pair_job=1;
    pair_process_channel(1);
    while(pair_job!=2)sched_yield();
    __sync_synchronize();
    pair_job=0;
    input_samples+=scans;
    cluster_samples+=pair_out;
   }

   if(diversity&&direct_dl_mode){
    div_count=scans;__sync_synchronize();div_job=1;
    div_process_channel(0);
    while(div_job!=2)sched_yield();
    __sync_synchronize();div_job=0;
    diversity_merge_captured();
   }
   if(diversity)diversity_flush(0);
   if(scan_dl_mode){
    double t=now();
    if(scan_following&&t-scan_last_crc_at>30.0){
     scan_following=0;
     scan_follow_hz=0;
     scan_index=0;
     scan_clear_discovery();
     if(!scan_retune(osc,scan_candidates[scan_index])){failures++;break;}
    }else if(!scan_following&&scan_target_ul_hz>0&&scan_best_candidate()>=0){
     int best=scan_best_candidate();
     scan_follow_hz=scan_discovered_dl[best];
     scan_following=1;
     scan_last_crc_at=t;
     if(scan_follow_hz!=active_lo_hz&&!scan_retune(osc,scan_follow_hz)){failures++;break;}
    }else if(!scan_following&&t-scan_tuned_at>6.0){
     scan_candidate_power[scan_index]=dp[1].power;
     if(scan_index+1<scan_count){
      scan_index++;
      if(!scan_retune(osc,scan_candidates[scan_index])){failures++;break;}
     }else{
      int best=scan_best_candidate();
      if(best>=0){
       scan_follow_hz=scan_discovered_dl[best];
       scan_following=1;
       scan_last_crc_at=t;
       if(scan_follow_hz!=active_lo_hz&&!scan_retune(osc,scan_follow_hz)){failures++;break;}
      }else{
       scan_index=0;
       scan_clear_discovery();
       if(!scan_retune(osc,scan_candidates[0])){failures++;break;}
      }
     }
    }
   }

  if(now()-last>1){
   spin_lock(&event_spin);
   arrays();
   if(event_log)fflush(event_log);
   if(debug_tetra)fflush(NULL);
   spin_unlock(&event_spin);
   status("receiving");
   last=now();
  }
 }

 if(fpga_stream_thread_started){fpga_stream_flush();__atomic_store_n(&ul_stream_stopping,1,__ATOMIC_RELEASE);pthread_join(fpga_stream_thread,0);}
 if(fpga_ul_thread_started){__atomic_store_n(&fpga_ul_stopping,1,__ATOMIC_RELEASE);pthread_join(fpga_ul_thread,0);}
 if(div_thread_started){__sync_synchronize();div_job=3;pthread_join(div_thread,0);}
 if(diversity)diversity_flush(1);
 if(pair_thread_started){
  __sync_synchronize();
  pair_job=3;
  pthread_join(pair_thread,0);
 }
 spin_lock(&event_spin);
 arrays();
 if(event_log){fflush(event_log);fclose(event_log);event_log=0;}
 if(debug_tetra)fflush(NULL);
 spin_unlock(&event_spin);
 iio_buffer_destroy(buf);
 iio_context_destroy(ctx);
 if(capture_only){FILE*f=fopen("capture.sc16","wb");if(f){fwrite(capture_iq,4,input_samples<CAPTURE_SCANS?input_samples:CAPTURE_SCANS,f);fclose(f);}}
 status(failures?"iio_read_error":"stopped");
 return failures?1:0;
}
