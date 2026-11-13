#ifndef TETRA_HISTORY_FILTER_H
#define TETRA_HISTORY_FILTER_H
#define TETRA_RETENTION_SECONDS 7200
#define TETRA_BUCKET_SECONDS 300
/* Canonical receiver JSON. Share predicates between RAM indexes and queries. */
static int history_matches(const char *line,int filter){
 if(filter==1)return strstr(line,"\"position\":{")!=0;
 if(filter==2)return strstr(line,"\"direction\":\"uplink\"")&&strstr(line,"\"crc_ok\":true");
 if(filter==3)return strstr(line,"\"direction\":\"downlink\"")&&strstr(line,"\"crc_ok\":true")&&strstr(line,"\"call_event\":1");
 if(filter==4)return strstr(line,"\"crc_ok\":true")&&(strstr(line,"\"sds\":1")||strstr(line,"\"cmce_type\":15,")||strstr(line,"\"cmce_type\":8,"));
 return 1;
}
#endif
