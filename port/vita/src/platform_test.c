#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include "debugScreen.h"
extern int halo_platform_contract(void (*check)(const char*,int));
static FILE *report;
static void check(const char *name,int pass){
    psvDebugScreenPrintf("%s: %s\n",name,pass?"PASS":"FAIL");
    if(report){fprintf(report,"%s: %s\n",name,pass?"PASS":"FAIL");fflush(report);}
}
int main(void){
    SceCtrlData pad;int failures;
    sceIoMkdir("ux0:data/halo-vita-diagnostic",0777);
    report=fopen("ux0:data/halo-vita-diagnostic/platform-02.txt","w");
    psvDebugScreenInit();psvDebugScreenPrintf("HALO VITA PLATFORM CONTRACT 0.2\n");
    failures=halo_platform_contract(check);check("PLATFORM_CONTRACT",failures==0);
    if(report)fclose(report);
    psvDebugScreenPrintf("SELECT + START: salir\n");
    do {sceCtrlPeekBufferPositive(0,&pad,1);sceKernelDelayThread(16000);} while((pad.buttons&(SCE_CTRL_SELECT|SCE_CTRL_START))!=(SCE_CTRL_SELECT|SCE_CTRL_START));
    sceKernelExitProcess(failures?1:0);return 0;
}
