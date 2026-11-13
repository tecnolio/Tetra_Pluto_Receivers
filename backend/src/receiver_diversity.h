/* Two demodulators, one ordered event/voice stream. No PCM or RF summing. */
#ifndef RECEIVER_DIVERSITY_H
#define RECEIVER_DIVERSITY_H
#define DIV_PENDING 32
typedef struct {
 int used,mask,hz,is_ul,score[2],good[2];
 double key,offered,end[2];
 Channel*c[2];DLDecoder*d[2];uint64_t abs_start[2];
 uint8_t bits[2][510];Event event[2];
} DiversityPending;
static DiversityPending div_pending[DIV_PENDING];
static double div_last_dl=-1;
static uint32_t div_expected_seed;
static int div_branch(Channel*c){return direct_ul_mode?(int)(c-ul)%2:(c==&dl[1]?0:1);}
static double div_clock(void){return input_samples/(double)active_rf_fs;}
static void div_commit(DiversityPending*p){
 int b=(p->mask&1)?0:1;
 if(p->mask==3){div_paired++;if(p->score[1]>p->score[0])b=1;}else div_single++;
 div_selected[b]++;
 if(b==1&&p->good[1]&&(!(p->mask&1)||!p->good[0]))div_recovered++;
 if(p->is_ul)record_event(p->c[b],&p->event[b],p->key);
 else{
  if(p->key<=div_last_dl+0.006){div_late++;p->used=0;return;}
  div_last_dl=p->key;
  dl_process_slot(p->c[b],p->d[b],p->bits[b],p->abs_start[b]);
 }
 p->used=0;
}
static void diversity_flush(int force){
 for(;;){
  DiversityPending*first=0;
  for(int i=0;i<DIV_PENDING;i++)if(div_pending[i].used&&(!first||div_pending[i].key<first->key))first=&div_pending[i];
  if(!first||(!force&&div_clock()-first->offered<0.04))break;
  div_commit(first);
 }
}
static DiversityPending*div_find(int branch,int hz,double key,int is_ul){
 DiversityPending*empty=0;
 for(int i=0;i<DIV_PENDING;i++){
  DiversityPending*p=&div_pending[i];if(!p->used){if(!empty)empty=p;continue;}
  double delta=p->key-key;if(delta<0)delta=-delta;
  if(p->hz==hz&&p->is_ul==is_ul&&!(p->mask&(1<<branch))&&delta<(is_ul?0.004:0.003))return p;
 }
 if(!empty){div_overflow++;return 0;}
 memset(empty,0,sizeof(*empty));empty->used=1;empty->hz=hz;empty->is_ul=is_ul;empty->key=key;empty->offered=div_clock();return empty;
}
static void diversity_ul_offer(Channel*c,Event*e,double start,double end){
 int b=div_branch(c);if(e->crc_ok)div_good[b]++;else div_bad[b]++;
 DiversityPending*p=div_find(b,c->frequency,start,1);if(!p)return;
 p->mask|=1<<b;p->c[b]=c;p->event[b]=*e;p->end[b]=end;
 p->good[b]=e->crc_ok;p->score[b]=e->crc_ok?100000-e->metric:-e->metric;
}
static int div_fec_score(const uint8_t*raw,int n,uint32_t seed){
 Event e={0};if(!pt_fec(raw,n,seed,&e))return 0;
 return 10000-(e.metric>999?999:e.metric);
}
static int div_slot_score(DLDecoder*d,const uint8_t*slot,uint64_t abs){
 int ss=dl_score_at(slot,dl_sync_pat,214,38),n1=dl_score_at(slot,dl_n1_pat,244,22),n2=dl_score_at(slot,dl_n2_pat,244,22),n3=dl_score_at(slot,dl_n3_pat,244,22);
 if(ss>=34){
  Event e={0};if(!pt_fec(slot+94,120,3,&e))return 0;
  e.direction=2;e.training=2;pt_protocol(&e);
  /* Each antenna retains its own timing anchor, even when the other wins. */
  if(e.network_info_valid&&e.mcc&&e.mnc){uint32_t cell=dl_cell_seed(e.mcc,e.mnc,e.colour_code);if(div_expected_seed&&cell!=div_expected_seed)return -1;div_expected_seed=cell;d->cell_mcc=e.mcc;d->cell_mnc=e.mnc;d->cell_colour=e.colour_code;d->cell_seed=dl_cell_seed(e.mcc,e.mnc,e.colour_code);d->have_cell=1;d->anchor_fn=e.fn;d->anchor_tn=e.tn;d->anchor_abs=abs;}
  return 10000-(e.metric>999?999:e.metric);
 }
 if(!d->have_cell)return 0;
 if(div_expected_seed&&d->cell_seed!=div_expected_seed)return -1;
 uint8_t raw[432],aach[30];memcpy(aach,slot+230,14);memcpy(aach+14,slot+266,16);
 memcpy(raw,slot+14,216);memcpy(raw+216,slot+282,216);
 int usage=voice_usage(aach,d->cell_seed),frame=d->anchor_fn;
 int delta=(int)(((long long)abs-(long long)d->anchor_abs)/510);
 if(frame>=1&&frame<=18)frame=(((frame-1)+(d->anchor_tn-1+delta)/4)%18+18)%18+1;
 if(n1>=19&&n1>=n2&&n1>=n3){
  if(usage>=4&&frame!=18)return 10000-4000*voice_quality(raw,d->cell_seed,0);
  return div_fec_score(raw,432,d->cell_seed);
 }
 if(n2>=19&&n2>=n3){
  int first=div_fec_score(raw,216,d->cell_seed);
  Event e={0};int stolen=0;
  if(first&&pt_fec(raw,216,d->cell_seed,&e)&&e.type1_length>=13&&e.mac_kind==0){int length=0;for(int i=7;i<13;i++)length=(length<<1)|e.payload[i];stolen=length==62;}
  int second=(usage>=4&&frame!=18&&!stolen)?10000-8000*voice_quality(raw,d->cell_seed,1):div_fec_score(raw+216,216,d->cell_seed);
  return(first+second)/2;
 }
 return 0;
}
static void diversity_dl_merge(Channel*c,DLDecoder*d,const uint8_t*slot,uint64_t abs,double key){
 /* Estimate physical slot position from the common DMA clock, not independent
    decoder bit counters. This also timestamps acquisition-window replays. */

 if(key<=div_last_dl+0.006){div_late++;return;}
 int b=div_branch(c),score=div_slot_score(d,slot,abs);
 if(score>=8000)div_good[b]++;else div_bad[b]++;
 if(score<0)return;
 DiversityPending*p=div_find(b,c->frequency,key,0);if(!p)return;
 p->mask|=1<<b;p->c[b]=c;p->d[b]=d;p->abs_start[b]=abs;
 memcpy(p->bits[b],slot,510);p->score[b]=score;p->good[b]=score>=8000;
}
#ifndef DIVERSITY_SELECTION_UNIT_TEST
/* Capture is private to each DSP thread. All FEC selection and speech state
   run on the main thread after the worker barrier. */
typedef struct{Channel*c;DLDecoder*d;uint64_t abs;double key;uint8_t bits[510];}DivCaptured;
static DivCaptured div_capture[2][DIV_PENDING];static int div_capture_count[2],div_capture_overflow[2];
static void diversity_dl_offer(Channel*c,DLDecoder*d,const uint8_t*bits,uint64_t abs){
 int b=div_branch(c),n=div_capture_count[b];
 if(n>=DIV_PENDING){div_capture_overflow[b]++;return;}
 DivCaptured*p=&div_capture[b][n];p->c=c;p->d=d;p->abs=abs;
 p->key=div_capture_sample[b]/(double)active_rf_fs-((double)d->total_raw*2.0-(double)abs)/36000.0;
 memcpy(p->bits,bits,510);div_capture_count[b]++;
}
static void diversity_merge_captured(void){
 int at[2]={0};
 for(;;){
  int b=at[0]<div_capture_count[0]?0:1;
  if(at[b]>=div_capture_count[b])break;
  if(at[1]<div_capture_count[1]&&(at[0]>=div_capture_count[0]||div_capture[1][at[1]].key<div_capture[0][at[0]].key))b=1;
  DivCaptured*p=&div_capture[b][at[b]++];diversity_dl_merge(p->c,p->d,p->bits,p->abs,p->key);
 }
 for(int b=0;b<2;b++){div_overflow+=div_capture_overflow[b];div_capture_overflow[b]=0;div_capture_count[b]=0;}
}
#endif
#endif
