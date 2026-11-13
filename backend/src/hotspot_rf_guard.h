/* Request classification only: keep read-only dashboards and contact edits usable. */
static int hotspot_rf_mutation(const char *request)
{
    char path[2049];
    const char *p=request;
    int post=!strncmp(request,"POST ",5),n=0,query=0;
    while(*p && *p!=' ')p++;
    if(!*p)return 0;
    p++;
    while(*p && *p!=' ' && *p!='?' && n<2048)path[n++]=*p++;
    path[n]=0;
    query=*p=='?';
    if(!strcmp(path,"/api/start") || !strcmp(path,"/api/stop") ||
       !strcmp(path,"/api/tetra/start") || !strcmp(path,"/api/tetra/stop") ||
       !strcmp(path,"/api/dmr/start") || !strcmp(path,"/api/dmr/stop") ||
       !strcmp(path,"/api/dmr/workspace/reception/start") ||
       !strcmp(path,"/api/dmr/workspace/reception/stop"))return 1;
    if(!strcmp(path,"/api/tetra/fpga"))return post||query;
    if(!strcmp(path,"/api/tetra/networks"))return post||query;
    if(!strncmp(path,"/tetrapol/api/",14)){
        const char *s=path+9;
        if(!strncmp(s,"/api/profiles",13))return post;
        if(!strcmp(s,"/api/rf/start") || !strcmp(s,"/api/rf/stop") ||
           !strcmp(s,"/api/uplink/start") || !strcmp(s,"/api/uplink/stop") ||
           !strncmp(s,"/api/lab/",9))return post;
    }
    if(!strncmp(path,"/adsb/app/cgi-bin/",18)){
        const char *s=path+18;
        if(!strcmp(s,"start") || !strcmp(s,"stop") || !strcmp(s,"aero-start") || !strcmp(s,"aero-stop"))return 1;
    }
    return 0;
}
