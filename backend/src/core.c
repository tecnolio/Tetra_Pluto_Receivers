/* TETRA core: stable FEC/demod + conservative uplink CMCE metadata. */
#include "core.h"
#define PI 3.14159265358979323846f
static float wrap(float x){while(x>PI)x-=2*PI;while(x< -PI)x+=2*PI;return x;}
static const uint8_t outputs[16][2]={{0,15},{11,4},{6,9},{13,2},{5,10},{14,1},{3,12},{8,7},{15,0},{4,11},{9,6},{2,13},{10,5},{1,14},{12,3},{7,8}};

int pt_fec(const uint8_t *raw,int k,uint32_t seed,Event *e){
 if(k!=432&&k!=216&&k!=168&&k!=120){memset(e,0,sizeof(*e));e->status="invalid_block_length";return 0;}
 int a=k==432?103:k==216?101:k==168?13:11,n=k*2/3,info=n-20;
 uint8_t ds[432],di[432],prev[288][16],bitpath[288][16];int8_t mother[1152];int metric[16],next[16];
 for(int i=0;i<k;i++){uint32_t b=0;static const int taps[]={32,26,23,22,16,12,11,10,8,7,5,4,2,1};for(int j=0;j<14;j++)b^=seed>>(32-taps[j]);b&=1;seed=(seed>>1)|(b<<31);ds[i]=raw[i]^b;}
 for(int i=1;i<=k;i++)di[i-1]=ds[(a*i)%k];memset(mother,-1,sizeof(mother));
 for(int j=1;j<=k;j++){static const int p[]={0,1,2,5};int idx=8*((j-1)/3)+p[j-3*((j-1)/3)]-1;if(idx<n*4)mother[idx]=di[j-1];}
 for(int s=0;s<16;s++)metric[s]=s?100000:0;
 for(int t=0;t<n;t++){for(int s=0;s<16;s++)next[s]=100000;
  for(int s=0;s<16;s++)for(int b=0;b<2;b++){int ns=((s<<1)|b)&15,cost=metric[s],v=outputs[s][b];for(int q=0;q<4;q++)if(mother[4*t+q]>=0)cost+=mother[4*t+q]!=((v>>(3-q))&1);if(cost<next[ns]){next[ns]=cost;prev[t][ns]=s;bitpath[t][ns]=b;}}
  memcpy(metric,next,sizeof(metric));}
 int state=0;for(int t=n-1;t>=0;t--){e->payload[t]=bitpath[t][state];state=prev[t][state];}
 uint16_t crc=65535;for(int i=0;i<info+16;i++){crc^=e->payload[i]<<15;crc=(crc&32768)?(crc<<1)^0x1021:crc<<1;}
 e->metric=metric[0];e->crc=crc;e->crc_ok=crc==0x1d0f;e->physical_bits=k;e->type1_length=info;return e->crc_ok;
}

static int get(const uint8_t *b,int n,int *p,int count,int *ok){if(count<0||count>25||*p<0||*p+count>n){*ok=0;return 0;}int v=0;for(int j=0;j<count;j++)v=(v<<1)|b[(*p)++];return v;}
static int peek(const uint8_t *b,int n,int p,int count){int ok=1;return get(b,n,&p,count,&ok);}
static uint32_t fcs32(const uint8_t*b,int n){uint32_t c=0xffffffff;if(n<32)c<<=32-n;for(int i=0;i<n;i++){int f=b[i]^(c>>31);c<<=1;if(f)c^=0x04c11db7;}return ~c;}
static const char *cmce_u_name(int t){switch(t){
 case 0x00:return "U-ALERT";case 0x02:return "U-CONNECT";case 0x04:return "U-DISCONNECT";case 0x05:return "U-INFO";
 case 0x06:return "U-RELEASE";case 0x07:return "U-SETUP";case 0x08:return "U-STATUS";case 0x09:return "U-TX CEASED";
 case 0x0a:return "U-TX DEMAND";case 0x0e:return "U-CALL RESTORE";case 0x0f:return "U-SDS DATA";case 0x10:return "U-FACILITY";
 default:return "U-CMCE-UNKNOWN";}}
static int cmce_is_call(int t){switch(t){case 0x00:case 0x02:case 0x04:case 0x06:case 0x07:case 0x09:case 0x0a:case 0x0e:return 1;default:return 0;}}

static const char *cmce_d_name(int t){switch(t){
 case 0x00:return "D-ALERT";case 0x01:return "D-CALL-PROCEEDING";case 0x02:return "D-CONNECT";case 0x03:return "D-CONNECT-ACK";
 case 0x04:return "D-DISCONNECT";case 0x05:return "D-INFO";case 0x06:return "D-RELEASE";case 0x07:return "D-SETUP";
 case 0x08:return "D-STATUS";case 0x09:return "D-TX-CEASED";case 0x0a:return "D-TX-CONTINUE";case 0x0b:return "D-TX-GRANTED";
 case 0x0c:return "D-TX-WAIT";case 0x0d:return "D-TX-INTERRUPT";case 0x0e:return "D-CALL-RESTORE";
 case 0x0f:return "D-SDS-DATA";case 0x10:return "D-FACILITY";default:return "D-CMCE-UNKNOWN";}}
static int cmce_d_is_call(int t){switch(t){
 case 0x00:case 0x01:case 0x02:case 0x03:case 0x04:case 0x06:case 0x07:
 case 0x09:case 0x0a:case 0x0b:case 0x0c:case 0x0d:case 0x0e:return 1;default:return 0;}}

static int tetra_frequency_offset_hz(int code){
 static const int values[4]={0,6250,-6250,12500};
 return code>=0&&code<4?values[code]:0;
}

static int tetra_duplex_spacing_hz(int band,int code){
 /* ETSI TS 100 392-15 table 2; zero also means an unsupported/reserved pair. */
 static const int values[10][8]={
  {0,0,0,0,0,0,0,0},
  {1600000,4500000,0,0,0,0,0,0},
  {10000000,0,0,0,0,0,0,0},
  {10000000,36000000,0,8000000,18000000,0,0,0},
  {10000000,7000000,0,8000000,5000000,9500000,0,0},
  {10000000,0,0,0,0,0,0,0},
  {10000000,0,0,0,30000000,0,0,0},
  {0,0,0,0,30000000,0,0,0},
  {0,45000000,0,18000000,0,0,0,0},
  {0,45000000,0,18000000,39000000,0,0,0}
 };
 if(band<0||band>=10||code<0||code>=8)return 0;
 return values[band][code];
}

static void tetra_sysinfo_frequencies(Event*e){
 if(e->freq_band<=0||e->main_carrier<0)return;
 e->downlink_hz=e->freq_band*100000000+e->main_carrier*25000+
  tetra_frequency_offset_hz(e->freq_offset);
 e->duplex_spacing_hz=tetra_duplex_spacing_hz(e->freq_band,e->duplex_spacing);
 if(e->duplex_spacing_hz>0)
  e->uplink_hz=e->downlink_hz+(e->reverse_operation?e->duplex_spacing_hz:-e->duplex_spacing_hz);
}

static void dl_optional_party(const uint8_t*b,int n,int*p,int*ok,int setup,Event*e){
 if(!*ok||*p>=n)return;
 int optional=get(b,n,p,1,ok);
 if(!*ok||!optional)return;

 /* Notification indicator. */
 int present=get(b,n,p,1,ok);
 if(present)get(b,n,p,6,ok);

 /* D-SETUP also has a temporary address before calling party identity. */
 if(setup){
  present=get(b,n,p,1,ok);
  if(present)get(b,n,p,24,ok);
 }

 present=get(b,n,p,1,ok);
 if(!*ok||!present)return;
 int type=get(b,n,p,2,ok);
 e->transmitting_party_type=type;
 if(type==1||type==2){
  e->ssi=get(b,n,p,24,ok);
  if(type==2)get(b,n,p,24,ok);
 }
}

static int dl_mac_length(int v){
 if(v==0||v==0x3b||v==0x3c||v>=0x3e)return -1;
 return v<=0x3a?v:-1;
}

static void dl_channel_alloc(const uint8_t*b,int n,int*p,int*ok,Event*e){
 e->allocation_type=get(b,n,p,2,ok);
 e->timeslot_mask=get(b,n,p,4,ok);
 e->allocation_ul_dl=get(b,n,p,2,ok);
 get(b,n,p,1,ok);get(b,n,p,1,ok);
 e->allocated_carrier=get(b,n,p,12,ok);
 int ext=get(b,n,p,1,ok);
 if(ext){e->allocated_band=get(b,n,p,4,ok);e->allocated_offset=get(b,n,p,2,ok);get(b,n,p,3,ok);get(b,n,p,1,ok);}
 int mp=get(b,n,p,2,ok);if(mp==0)get(b,n,p,2,ok);
 if(e->allocation_ul_dl==0){
  get(b,n,p,2,ok);get(b,n,p,3,ok);get(b,n,p,3,ok);get(b,n,p,3,ok);get(b,n,p,3,ok);
  get(b,n,p,3,ok);get(b,n,p,4,ok);get(b,n,p,5,ok);
  int nap=get(b,n,p,2,ok);if(nap==1)get(b,n,p,11,ok);
  get(b,n,p,4,ok);if(get(b,n,p,1,ok))get(b,n,p,16,ok);
  if(get(b,n,p,1,ok))get(b,n,p,16,ok);get(b,n,p,1,ok);
 }
 e->allocation_valid=*ok;
}

static void dl_basic_service(Event*e,int value){
 e->basic_service=value;e->circuit_mode=(value>>5)&7;
 e->service_encrypted=(value>>4)&1;e->communication_type=(value>>2)&3;
 e->speech_service=value&3;
 e->destination_kind=e->communication_type?2:1;
 if(e->communication_type&&e->called_ssi>0)e->gssi=e->called_ssi;
}

static void pt_protocol_dl_normal(Event*e){
 const uint8_t*b=e->payload;int n=e->type1_length,p=0,ok=1;
 int pdu=get(b,n,&p,2,&ok);e->mac_kind=pdu;
 static const char*mac_names[]={"DL-MAC-RESOURCE","DL-MAC-FRAG-END","DL-BROADCAST","DL-MAC-SUPPL"};
 e->status=ok?mac_names[pdu&3]:"truncated_dl_mac";
 if(!ok||pdu!=0)return;

 p=4;
 int enc=get(b,n,&p,2,&ok);get(b,n,&p,1,&ok);
 int length_ind=get(b,n,&p,6,&ok),at=get(b,n,&p,3,&ok),addr=0,usage=-1;
 e->encrypted=enc!=0;e->address_type=at;
 if(!ok){e->status="truncated_dl_resource";return;}
 switch(at){
  case 0:e->status="MAC-NULL";return;
  case 1:case 3:case 4:addr=get(b,n,&p,24,&ok);break;
  case 2:get(b,n,&p,10,&ok);break;
  case 5:case 7:addr=get(b,n,&p,24,&ok);get(b,n,&p,10,&ok);break;
  case 6:addr=get(b,n,&p,24,&ok);usage=get(b,n,&p,6,&ok);break;
  default:ok=0;break;
 }
 if(!ok){e->status="truncated_dl_address";return;}
 e->mac_address=addr;

 int power=get(b,n,&p,1,&ok);if(power)get(b,n,&p,4,&ok);
 int grant=get(b,n,&p,1,&ok);if(grant){get(b,n,&p,4,&ok);get(b,n,&p,4,&ok);}
 e->usage_marker=usage;
 int alloc=get(b,n,&p,1,&ok);if(alloc&&enc==0)dl_channel_alloc(b,n,&p,&ok,e);
 if(!ok){e->status="truncated_dl_resource";return;}
 if(enc){e->status="encrypted_not_processed";return;}

 int mac_len=dl_mac_length(length_ind);
 if(mac_len<=0||p>=n)return;
 int end=p+mac_len*8;if(end>n)end=n;
 int raw_llc=get(b,end,&p,4,&ok);e->llc_type=raw_llc;
 if(!ok||raw_llc>10){e->status="unsupported_dl_llc";return;}
 int effective=end;
 if(raw_llc>=4&&raw_llc<=7){effective-=32;if(effective<p){e->status="truncated_dl_fcs";return;}}
 int base=raw_llc<=7?(raw_llc&3):raw_llc;
 if(base==0){get(b,effective,&p,1,&ok);get(b,effective,&p,1,&ok);}
 else if(base==1)get(b,effective,&p,1,&ok);
 else if(base==2){}
 else if(base==3){e->status="DL-LLC-ACK";return;}
 else{e->status="unsupported_dl_llc";return;}
 if(!ok||p+8>effective){e->status="truncated_dl_llc";return;}

 e->mle_protocol=get(b,effective,&p,3,&ok);
 if(!ok||e->mle_protocol!=2){e->status="DL-MLE-other";return;}
 e->cmce_type=get(b,effective,&p,5,&ok);
 if(!ok){e->status="truncated_dl_cmce";return;}
 e->status=cmce_d_name(e->cmce_type);
 e->called_party_type=addr?1:0;
 e->called_ssi=addr;
 e->call_event=cmce_d_is_call(e->cmce_type);

 if(e->cmce_type==0x02&&p+24<=effective){
  e->call_id=get(b,effective,&p,14,&ok);get(b,effective,&p,4,&ok);get(b,effective,&p,1,&ok);get(b,effective,&p,1,&ok);
  int tx=get(b,effective,&p,2,&ok);get(b,effective,&p,1,&ok);get(b,effective,&p,1,&ok);e->tx_active=tx!=0;
 }else if(e->cmce_type==0x06&&p+25<=effective){
  e->call_id=get(b,effective,&p,14,&ok);get(b,effective,&p,5,&ok);get(b,effective,&p,6,&ok);e->tx_active=0;
 }else if(e->cmce_type==0x07&&p+35<=effective){
  e->call_id=get(b,effective,&p,14,&ok);get(b,effective,&p,4,&ok);get(b,effective,&p,1,&ok);get(b,effective,&p,1,&ok);
  int service=get(b,effective,&p,8,&ok);if(ok)dl_basic_service(e,service);
  int tx=get(b,effective,&p,2,&ok);get(b,effective,&p,1,&ok);get(b,effective,&p,4,&ok);
  e->transmission_grant=tx;e->tx_active=tx==0||tx==3;
  dl_optional_party(b,effective,&p,&ok,1,e);
 }else if(e->cmce_type>=0x09&&e->cmce_type<=0x0d&&p+14<=effective){
  e->call_id=get(b,effective,&p,14,&ok);
  e->tx_active=e->cmce_type==0x0a||e->cmce_type==0x0b;
  if(e->cmce_type==0x0b&&p+5<=effective){
   e->transmission_grant=get(b,effective,&p,2,&ok);
   get(b,effective,&p,1,&ok);e->service_encrypted=get(b,effective,&p,1,&ok);get(b,effective,&p,1,&ok);
   e->tx_active=e->transmission_grant==0||e->transmission_grant==3;
   dl_optional_party(b,effective,&p,&ok,0,e);
  }
 }else if(e->cmce_type==0x0f){
  e->sds=1;
  int cpti=get(b,effective,&p,2,&ok),calling=0;
  if(cpti==0)calling=get(b,effective,&p,8,&ok);
  else if(cpti==1)calling=get(b,effective,&p,24,&ok);
  else if(cpti==2){calling=get(b,effective,&p,24,&ok);get(b,effective,&p,24,&ok);}
  else ok=0;
  if(ok)e->ssi=calling;
 }
 (void)usage;
 if(!ok){e->status="truncated_dl_cmce";e->gssi=0;e->allocation_valid=0;}
 else e->call_fields_valid=1;
}

void pt_protocol(Event *e){
 e->status="crc_failed";e->position_valid=e->sds=e->lip=0;e->ssi=0;e->speed=e->heading=-1;
 e->mac_kind=e->address_type=e->mac_address=e->llc_type=e->mle_protocol=e->cmce_type=0;
 e->called_party_type=e->called_ssi=e->call_id=e->call_event=e->tx_active=0;
 e->transmission_grant=e->transmitting_party_type=0;
 e->basic_service=e->circuit_mode=e->communication_type=e->service_encrypted=e->speech_service=-1;
 e->gssi=e->destination_kind=e->call_fields_valid=e->allocation_valid=0;
 e->allocation_type=e->allocated_carrier=e->timeslot_mask=e->allocation_ul_dl=0;
 e->allocated_band=e->allocated_offset=-1;e->allocated_downlink_hz=0;e->usage_marker=-1;
 e->network_info_valid=e->dl_broadcast_type=e->colour_code=e->tn=e->fn=e->mn=e->mcc=e->mnc=0;
 e->main_carrier=e->freq_band=e->freq_offset=e->duplex_spacing=e->reverse_operation=e->num_of_csch=e->la=e->subscr_class=e->bs_service_details=0;
 e->downlink_hz=e->uplink_hz=e->duplex_spacing_hz=0;
 if(!e->crc_ok)return;
 const uint8_t*b=e->payload;int n=e->type1_length,p=0,ok=1,hu=n==92;
 if(e->direction==2&&e->training!=2&&(n==124||n==268)){pt_protocol_dl_normal(e);return;}
 if(e->direction==2&&n==60){
  e->network_info_valid=1;
  e->colour_code=peek(b,n,4,6);e->tn=peek(b,n,10,2)+1;e->fn=peek(b,n,12,5);e->mn=peek(b,n,17,6);
  e->mcc=peek(b,n,31,10);e->mnc=peek(b,n,41,14);
  e->status="DL-SYNC-SB1";
  return;
 }
 int kind=get(b,n,&p,hu?1:2,&ok);e->mac_kind=kind;e->status="unsupported_mac";if(!ok)return;
 if(e->direction==2&&kind==2&&n>=55){
  e->network_info_valid=1;
  e->colour_code=peek(b,n,4,6);e->tn=peek(b,n,10,2)+1;e->fn=peek(b,n,12,5);e->mn=peek(b,n,17,6);
  e->mcc=peek(b,n,31,10);e->mnc=peek(b,n,41,14);
  e->dl_broadcast_type=peek(b,n,2,2);
  if(n>=124&&e->dl_broadcast_type==0){
   e->main_carrier=peek(b,n,4,12);e->freq_band=peek(b,n,16,4);e->freq_offset=peek(b,n,20,2);
   e->duplex_spacing=peek(b,n,22,3);e->reverse_operation=peek(b,n,25,1);e->num_of_csch=peek(b,n,26,2);
   e->la=peek(b,n,82,14);e->subscr_class=peek(b,n,96,16);e->bs_service_details=peek(b,n,112,12);
   tetra_sysinfo_frequencies(e);
   e->status="DL-SYSINFO";
  }else if(e->dl_broadcast_type==1)e->status="DL-ACCESS-DEFINE";
  else e->status="DL-BROADCAST";
  return;
 }
 if(kind)return;
 int fill=get(b,n,&p,1,&ok);e->encrypted=get(b,n,&p,1,&ok);
 int at=get(b,n,&p,2,&ok),address=get(b,n,&p,at==1?10:24,&ok);e->address_type=at;e->mac_address=address;
 if(!ok){e->status="truncated_mac";return;}if(e->encrypted){e->status="encrypted_not_processed";return;}if(at==0)e->ssi=address;
 int optional=hu?get(b,n,&p,1,&ok):1,end=n;
 if(optional){int capacity=get(b,n,&p,1,&ok);if(capacity){e->status="fragment_not_implemented";return;}int length=get(b,n,&p,hu?5:6,&ok);if(!length){e->status="MAC-NULL";return;}if(length<3||length>(hu?12:34)){e->status="bad_mac_length";return;}end=length*8;if(end>n&&end==((n+7)/8)*8)end=n;}
 if(!ok||end>n||p>end){e->status="truncated_mac";return;}if(fill){while(end>p&&!b[end-1])end--;if(end<=p){e->status="bad_fill";return;}end--;}
 int llc=get(b,end,&p,4,&ok),base=llc%4;e->llc_type=llc;e->status="unsupported_llc";if(!ok||llc>=8)return;
 if(base==0||base==3)get(b,end,&p,1,&ok);if(base==0||base==1)get(b,end,&p,1,&ok);
 if(llc>=4){if(end-p<32){e->status="truncated_fcs";return;}uint32_t rx=0;for(int i=end-32;i<end;i++)rx=(rx<<1)|b[i];end-=32;if(fcs32(b+p,end-p)!=rx){e->status="llc_fcs_failed";return;}}
 if(base==3){e->status="LLC-ACK";return;}if(!ok)return;
 e->mle_protocol=get(b,end,&p,3,&ok);if(!ok){e->status="truncated_mle";return;}if(e->mle_protocol!=2){e->status="MLE-other";return;}
 e->cmce_type=get(b,end,&p,5,&ok);if(!ok){e->status="truncated_cmce";return;}e->call_event=cmce_is_call(e->cmce_type);e->status=cmce_u_name(e->cmce_type);

 /* ETSI EN 300 392-2 U-CMCE mandatory fields used by Call Flow. */
 if(e->cmce_type==0x07){ /* U-SETUP */
  get(b,end,&p,4,&ok); /* area selection */
  get(b,end,&p,1,&ok); /* hook */
  get(b,end,&p,1,&ok); /* simplex/duplex */
  get(b,end,&p,8,&ok); /* basic service */
  e->tx_active=get(b,end,&p,1,&ok);
  get(b,end,&p,4,&ok); /* priority */
  get(b,end,&p,2,&ok); /* CLIR */
  e->called_party_type=get(b,end,&p,2,&ok);
  if(e->called_party_type==0)e->called_ssi=get(b,end,&p,8,&ok);
  else if(e->called_party_type==1)e->called_ssi=get(b,end,&p,24,&ok);
  else if(e->called_party_type==2){e->called_ssi=get(b,end,&p,24,&ok);get(b,end,&p,24,&ok);}
  if(!ok)e->status="truncated_U-SETUP";
  return;
 }
 if(e->cmce_type==0x09){e->call_id=get(b,end,&p,14,&ok);e->tx_active=0;if(!ok)e->status="truncated_U-TX_CEASED";return;}
 if(e->cmce_type==0x0a){e->call_id=get(b,end,&p,14,&ok);get(b,end,&p,2,&ok);get(b,end,&p,1,&ok);get(b,end,&p,1,&ok);e->tx_active=1;if(!ok)e->status="truncated_U-TX_DEMAND";return;}
 if(e->cmce_type==0x06){e->call_id=get(b,end,&p,14,&ok);get(b,end,&p,5,&ok);e->tx_active=0;if(!ok)e->status="truncated_U-RELEASE";return;}
 if(e->cmce_type==0x0e){e->call_id=get(b,end,&p,14,&ok);e->tx_active=get(b,end,&p,1,&ok);e->called_party_type=get(b,end,&p,2,&ok);if(e->called_party_type==0)e->called_ssi=get(b,end,&p,8,&ok);else if(e->called_party_type==1)e->called_ssi=get(b,end,&p,24,&ok);else if(e->called_party_type==2){e->called_ssi=get(b,end,&p,24,&ok);get(b,end,&p,24,&ok);}if(!ok)e->status="truncated_U-CALL_RESTORE";return;}
 if(e->cmce_type!=0x0f)return;

 /* Existing SDS/LIP path. */
 get(b,end,&p,4,&ok);int party=get(b,end,&p,2,&ok);if(party==3)return;get(b,end,&p,party==0?8:24,&ok);if(party==2)get(b,end,&p,24,&ok);
 int sdti=get(b,end,&p,2,&ok);if(sdti!=3){int width=16<<sdti;if(p+width<=end&&ok){e->sds=1;e->status="SDS-fixed";}return;}
 int length=get(b,end,&p,11,&ok);if(!ok||length<8||p+length>end){e->status="truncated_sds";return;}
 e->sds=1;e->status="SDS";int finish=p+length,protocol=get(b,end,&p,8,&ok);if(protocol!=10)return;e->lip=1;e->status="LIP-unsupported";
 int liptype=get(b,finish,&p,2,&ok);if(liptype)return;get(b,finish,&p,2,&ok);
 int lon=get(b,finish,&p,25,&ok),lat=get(b,finish,&p,24,&ok);if(lon&(1<<24))lon-=1<<25;if(lat&(1<<23))lat-=1<<24;
 int err=get(b,finish,&p,3,&ok),speed=get(b,finish,&p,7,&ok),heading=get(b,finish,&p,4,&ok);int user=get(b,finish,&p,1,&ok);int reason=get(b,finish,&p,8,&ok);
 if(!ok||p!=finish){e->status="malformed_lip";return;}e->error_class=err;e->reason=user?-1:reason;e->speed_code=speed;e->heading_code=heading;
 if(err==7){e->status="lip_no_fix";return;}if(!e->ssi){e->status="lip_no_ssi";return;}
 e->longitude=lon*360.0/33554432;e->latitude=lat*180.0/16777216;e->speed=speed<126?(speed<=28?speed:16*pow(1.038,speed-13)):-1;e->heading=heading<15?heading*22.5:-1;e->position_valid=1;e->status="LIP-short-position";
}

static int hard(float a){static const float ph[]={-3*PI/4,-PI/4,PI/4,3*PI/4};int best=0;float score=10;for(int i=0;i<4;i++){float v=fabsf(wrap(a-ph[i]));if(v<score){score=v;best=i;}}static const int dib[]={3,2,0,1};return dib[best];}
typedef struct {float score,rot,timing;int pos,train;} Candidate;
static void bit_variant(uint8_t *dst,const uint8_t *src,int n,int variant){
 static const uint8_t map[8][4]={
  {0,1,2,3},{1,3,0,2},{3,2,1,0},{2,0,3,1},
  {2,3,0,1},{0,2,1,3},{1,0,3,2},{3,1,2,0}
 };
 int v=variant&7;
 for(int i=0;i+1<n;i+=2){
  uint8_t p=(src[i]<<1)|src[i+1],m=map[v][p&3];
  dst[i]=m>>1;dst[i+1]=m&1;
 }
 if(n&1)dst[n-1]=src[n-1];
}
static int try_fec_candidate(const uint8_t *bits,int nb,int a,int b,int count,int parts,int dir,Candidate h,int errors,uint32_t seed,Event *e){
 if(a<0||b<0||a+count/2>nb||b+count/2>nb)return 0;
 uint8_t raw[432],work[432];int half=count/2;
 memcpy(raw,bits+a,half);memcpy(raw+half,bits+b,half);
  int variants=dir==2?8:1;
 uint32_t seeds[2]={seed,3};
 int seed_count=dir==2?2:1;
 for(int variant=0;variant<variants;variant++){
  bit_variant(work,raw,count,variant);
  for(int si=0;si<seed_count;si++){
#ifdef PT_FEC_RESULT
   int collected=0;
#endif
   for(int part=0;part<parts;part++){
    Event test={0};
    pt_fec(work+part*216,parts==2?216:count,seeds[si],&test);
    test.direction=dir;test.training=h.train;test.training_errors=errors;test.cfo_hz=h.rot*18000/(2*PI);test.bit_variant=variant;
    if(test.metric<e->metric||test.crc_ok){*e=test;pt_protocol(e);}
    if(test.crc_ok){
#ifdef PT_FEC_RESULT
     PT_FEC_RESULT(test,part,parts,h,errors);collected++;
#else
     return 1;
#endif
    }
   }
#ifdef PT_FEC_RESULT
   if(collected)return 1;
#endif
  }
 }
 return 0;
}
static int pt_demod_rate_mode(const IQ*x,int n,uint32_t seed,Event*e,float sample_rate,int ul_only){
 static const char *patterns[]={"1101000011101001110100","0111101001000011011110","11000001100111001110100111000001100111"};
 enum { MAX_SYMS=6000 };
 Candidate top[32];int ntop=0;float sps=sample_rate/18000,dp[MAX_SYMS],cd[MAX_SYMS],sd[MAX_SYMS];int ndp;
 for(int ti=0;ti<32;ti++){
  float timing=ti*sps/32;IQ previous={0,0};ndp=0;
  for(int s=0;;s++){float t=timing+s*sps;int i=(int)t;if(i+1>=n||s>=MAX_SYMS)break;float f=t-i;IQ cur={x[i].i*(1-f)+x[i+1].i*f,x[i].q*(1-f)+x[i+1].q*f};if(s)dp[ndp++]=atan2f(cur.q*previous.i-cur.i*previous.q,cur.i*previous.i+cur.q*previous.q);previous=cur;}
  for(int j=0;j<ndp;j++){cd[j]=cosf(dp[j]);sd[j]=sinf(dp[j]);}
  for(int tr=0;tr<3;tr++){const char*pat=patterns[tr];int l=strlen(pat)/2;float cp[24],sp[24];for(int j=0;j<l;j++){int bb=(pat[2*j]-'0')*2+pat[2*j+1]-'0';static const float map[]={PI/4,3*PI/4,-PI/4,-3*PI/4};cp[j]=cosf(map[bb]);sp[j]=sinf(map[bb]);}
   for(int pp=0;pp+l<=ndp;pp++){float si=0,co=0;for(int j=0;j<l;j++){si+=sd[pp+j]*cp[j]-cd[pp+j]*sp[j];co+=cd[pp+j]*cp[j]+sd[pp+j]*sp[j];}float score=1-sqrtf(si*si+co*co)/l;if(score>.12)continue;float rot=atan2f(si,co);if(fabsf(rot)>PI/3)continue;
    if(ntop==32&&score>=top[31].score)continue;int slot=ntop<32?ntop++:31;while(slot>0&&top[slot-1].score>score){if(slot<32)top[slot]=top[slot-1];slot--;}top[slot]=(Candidate){score,rot,timing,pp,tr};
   }
  }
 }
 memset(e,0,sizeof(*e));e->metric=100000;e->status="no_training";
 for(int k=0;k<ntop;k++){
  Candidate h=top[k];uint8_t bits[2*MAX_SYMS];int nb=0;IQ previous={0,0};
  for(int s=0;;s++){float t=h.timing+s*sps;int i=(int)t;if(i+1>=n||s>=MAX_SYMS)break;float f=t-i;IQ cur={x[i].i*(1-f)+x[i+1].i*f,x[i].q*(1-f)+x[i+1].q*f};if(s){int v=hard(atan2f(cur.q*previous.i-cur.i*previous.q,cur.i*previous.i+cur.q*previous.q)-h.rot);bits[nb++]=v>>1;bits[nb++]=v&1;}previous=cur;}
   int pp=h.pos*2,errors=0,l=strlen(patterns[h.train]);if(pp<0||pp+l>nb)continue;for(int j=0;j<l;j++)errors+=bits[pp+j]!=patterns[h.train][j]-'0';if(errors>(h.train==2?3:2))continue;
  if(ul_only){
   if(h.train==2){if(try_fec_candidate(bits,nb,pp-84,pp+30,168,1,1,h,errors,seed,e))return 1;}
   else{
    /* NUB may carry one 432-bit block (SDS/LIP) or two 216-bit blocks.
       Try the full block before the independently coded short blocks. */
    if(try_fec_candidate(bits,nb,pp-216,pp+22,432,1,1,h,errors,seed,e))return 1;
    if(try_fec_candidate(bits,nb,pp-216,pp+22,432,2,1,h,errors,seed,e))return 1;
   }
   continue;
  }
  if(h.train==2){
    if(try_fec_candidate(bits,nb,pp+68,pp+176,216,1,2,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-120,pp-60,120,1,2,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-84,pp+30,168,1,1,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-216,pp+30,432,1,2,h,errors,seed,e))return 1;
   for(int da=-12;da<=12;da+=4)
    for(int db=-12;db<=12;db+=4)
     if(try_fec_candidate(bits,nb,pp-216+da,pp+30+db,432,1,2,h,errors,seed,e))return 1;
  }else{
    if(try_fec_candidate(bits,nb,pp-230,pp+38,432,1,2,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-216,pp+22,432,2,1,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-230,pp+38,432,2,2,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-216,pp+22,432,2,2,h,errors,seed,e))return 1;
   if(try_fec_candidate(bits,nb,pp-216,pp+30,432,2,2,h,errors,seed,e))return 1;
   for(int da=-20;da<=20;da+=4)
    for(int db=-20;db<=20;db+=4)
     if(try_fec_candidate(bits,nb,pp-216+da,pp+30+db,432,2,2,h,errors,seed,e))return 1;
  }
 }
 return 0;
}

int pt_demod_rate(const IQ*x,int n,uint32_t seed,Event*e,float sample_rate){return pt_demod_rate_mode(x,n,seed,e,sample_rate,0);}
int pt_demod_ul_rate(const IQ*x,int n,uint32_t seed,Event*e,float sample_rate){return pt_demod_rate_mode(x,n,seed,e,sample_rate,1);}
int pt_demod(const IQ*x,int n,uint32_t seed,Event*e){
 return pt_demod_rate(x,n,seed,e,250000.0f);
}

int pt_json(char*out,int cap,const Event*e,int frequency){
 char pos[320]="null",speed[40]="null",heading[40]="null",payload_bits[289]="";
 const char *cmce_name=(e->crc_ok&&e->mle_protocol==2)?
  (e->direction==2?cmce_d_name(e->cmce_type):cmce_u_name(e->cmce_type)):"";
 if(e->speed>=0)snprintf(speed,sizeof(speed),"%.3f",e->speed);if(e->heading>=0)snprintf(heading,sizeof(heading),"%.3f",e->heading);
 if(e->crc_ok&&e->position_valid)snprintf(pos,sizeof(pos),"{\"ssi\":%d,\"latitude\":%.9f,\"longitude\":%.9f,\"speed_kmh\":%s,\"heading_deg\":%s,\"reason\":%d}",e->ssi,e->latitude,e->longitude,speed,heading,e->reason);
 if(e->type1_length>0){int nn=e->type1_length;if(nn>288)nn=288;for(int i=0;i<nn;i++)payload_bits[i]=e->payload[i]?'1':'0';payload_bits[nn]=0;}
 int used=snprintf(out,cap,
   "{\"time_s\":%.6f,\"frequency_hz\":%d,\"crc_ok\":%s,\"crc\":%d,\"fec_metric\":%d,\"direction\":\"%s\",\"physical_bits\":%d,\"type1_length\":%d,\"payload_bits\":\"%s\",\"cfo_hz\":%.3f,\"training\":%d,\"training_errors\":%d,\"bit_variant\":%d,\"ssi\":%d,\"sds\":%d,\"lip\":%d,\"mac_kind\":%d,\"address_type\":%d,\"mac_address\":%d,\"llc_type\":%d,\"mle_protocol\":%d,\"cmce_type\":%d,\"cmce_name\":\"%s\",\"call_event\":%d,\"call_id\":%d,\"called_party_type\":%d,\"called_ssi\":%d,\"tx_active\":%d,\"transmission_grant\":%d,\"transmitting_party_type\":%d,\"network_info_valid\":%d,\"dl_broadcast_type\":%d,\"colour_code\":%d,\"tn\":%d,\"fn\":%d,\"mn\":%d,\"mcc\":%d,\"mnc\":%d,\"main_carrier\":%d,\"freq_band\":%d,\"freq_offset\":%d,\"duplex_spacing\":%d,\"reverse_operation\":%d,\"num_of_csch\":%d,\"la\":%d,\"subscr_class\":%d,\"bs_service_details\":%d,\"downlink_hz\":%d,\"uplink_hz\":%d,\"duplex_spacing_hz\":%d,\"status\":\"%s\",\"position\":%s}",
   e->time_s,frequency,e->crc_ok?"true":"false",e->crc,e->metric,e->crc_ok?(e->direction==1?"uplink":"downlink"):"unconfirmed",e->physical_bits,e->type1_length,payload_bits,e->cfo_hz,e->training,e->training_errors,e->bit_variant,e->ssi,e->sds,e->lip,e->mac_kind,e->address_type,e->mac_address,e->llc_type,e->mle_protocol,e->cmce_type,cmce_name,e->call_event,e->call_id,e->called_ssi?e->called_party_type:0,e->called_ssi,e->tx_active,e->transmission_grant,e->transmitting_party_type,e->network_info_valid,e->dl_broadcast_type,e->colour_code,e->tn,e->fn,e->mn,e->mcc,e->mnc,e->main_carrier,e->freq_band,e->freq_offset,e->duplex_spacing,e->reverse_operation,e->num_of_csch,e->la,e->subscr_class,e->bs_service_details,e->downlink_hz,e->uplink_hz,e->duplex_spacing_hz,e->status?e->status:"unknown",pos);
 if(used<=0||used>=cap)return used;
 int extra=snprintf(out+used-1,cap-used+1,
  ",\"protocol_version\":2,\"encrypted\":%d,\"basic_service\":%d,\"circuit_mode\":%d,\"communication_type\":%d,\"service_encrypted\":%d,\"speech_service\":%d,\"gssi\":%d,\"destination_kind\":%d,\"call_fields_valid\":%d,\"usage_marker\":%d,\"allocation_valid\":%d,\"allocation_type\":%d,\"allocated_carrier\":%d,\"timeslot_mask\":%d,\"allocation_ul_dl\":%d,\"allocated_downlink_hz\":%d}",
  e->encrypted,e->basic_service,e->circuit_mode,e->communication_type,e->service_encrypted,e->speech_service,e->gssi,e->destination_kind,e->call_fields_valid,e->usage_marker,e->allocation_valid,e->allocation_type,e->allocated_carrier,e->timeslot_mask,e->allocation_ul_dl,e->allocated_downlink_hz);
 return used-1+extra;
}

int pt_analyze(const IQ*x,int n,uint32_t seed,char*out,int cap){Event e;int ok=pt_demod(x,n,seed,&e);pt_json(out,cap,&e,0);return ok;}
int pt_payload_json(const uint8_t*bits,int n,int crc_ok,char*out,int cap){Event e={0};if(n!=268&&n!=124&&n!=92)return -1;memcpy(e.payload,bits,n);e.type1_length=n;e.crc_ok=crc_ok;e.direction=1;pt_protocol(&e);pt_json(out,cap,&e,0);return e.position_valid;}
