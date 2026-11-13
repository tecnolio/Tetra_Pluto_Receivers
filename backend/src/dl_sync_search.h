#ifndef PT_DL_SYNC_SEARCH_H
#define PT_DL_SYNC_SEARCH_H

/* Same exhaustive Hamming search as the original nested loop, using a rolling
 * 38-bit window, avoiding a separate comparison of every training bit at
 * every possible offset on the Cortex-A9. */
static void dl_find_sync38(const uint8_t*bits,int count,const uint8_t*pattern,int*best,int*position){
 uint64_t target=0,window=0;
 const uint64_t mask=((uint64_t)1<<38)-1;
 *best=0;*position=-1;
 for(int i=0;i<38;i++)target=(target<<1)|(pattern[i]&1);
 for(int i=0;i<count;i++){
  window=((window<<1)|(bits[i]&1))&mask;
  if(i<37)continue;
  int score=38-__builtin_popcountll(window^target);
  if(score>=*best){*best=score;*position=i-37;}
 }
}

static uint64_t dl_slot_at_or_after(uint64_t start,uint64_t boundary){
 if(start>=boundary)return start;
 return start+((boundary-start+509)/510)*510;
}
#endif
