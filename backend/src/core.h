#ifndef PT_CORE_H
#define PT_CORE_H
#include "platform.h"

typedef struct { float i,q; } IQ;

typedef struct {
 int crc_ok,metric,crc,physical_bits,type1_length,direction,training,training_errors;
 int bit_variant;
 int ssi,sds,lip,position_valid,encrypted;
 int mac_kind,address_type,mac_address,llc_type,mle_protocol,cmce_type;
 int called_party_type,called_ssi,call_id,call_event,tx_active;
 int transmission_grant,transmitting_party_type;
 int basic_service,circuit_mode,communication_type,service_encrypted,speech_service;
 int gssi,destination_kind,call_fields_valid,usage_marker;
 int allocation_valid,allocation_type,allocated_carrier,timeslot_mask,allocation_ul_dl;
 int allocated_band,allocated_offset,allocated_downlink_hz;
 int network_info_valid,dl_broadcast_type,colour_code,tn,fn,mn,mcc,mnc;
 int main_carrier,freq_band,freq_offset,duplex_spacing,reverse_operation,num_of_csch,la,subscr_class,bs_service_details;
 int downlink_hz,uplink_hz,duplex_spacing_hz;
 double cfo_hz,latitude,longitude,speed,heading,time_s;
 int error_class,reason,speed_code,heading_code;
 const char *status;
 uint8_t payload[288];
} Event;

int pt_fec(const uint8_t*,int,uint32_t,Event*);
int pt_demod(const IQ*,int,uint32_t,Event*);
int pt_demod_rate(const IQ*,int,uint32_t,Event*,float);
int pt_demod_ul_rate(const IQ*,int,uint32_t,Event*,float);
void pt_protocol(Event*);
int pt_json(char*,int,const Event*,int);
#endif
