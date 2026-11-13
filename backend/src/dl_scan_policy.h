#ifndef PT_DL_SCAN_POLICY_H
#define PT_DL_SCAN_POLICY_H

/* A SYSINFO from another cell can suggest a candidate, never confirm it.
 * Follow only after that candidate has itself advertised the requested UL. */
static int dl_scan_matches_target(int target_ul,int announced_ul){
 if(target_ul<=0)return 1;
 if(announced_ul<=0)return 0;
 int delta=announced_ul-target_ul;
 return delta>=-12500&&delta<=12500;
}

static int dl_scan_target_hint(int target_ul,int announced_dl,int announced_ul){
 if(target_ul<70000000||target_ul>1000000000||announced_dl<=0||announced_ul<=0)return 0;
 if(target_ul/100000000!=announced_ul/100000000)return 0;
 int spacing=announced_dl-announced_ul;
 if(!spacing||spacing>50000000||spacing< -50000000)return 0;
 int hint=target_ul+spacing;
 return hint>=70000000&&hint<=1000000000?hint:0;
}
#endif
