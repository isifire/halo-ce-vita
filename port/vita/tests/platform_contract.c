/* Contract checks use the same XDK declarations and ABI as engine callers. */
#include <xtl.h>
#include <string.h>
#include <psp2/ctrl.h>
extern void halo_vita_convert_pad(unsigned int,unsigned char,unsigned char,unsigned char,unsigned char,XINPUT_GAMEPAD*);
static int callback_count;
static DWORD callback_error, callback_bytes;
static HANDLE gate;
static void WINAPI completed(DWORD error,DWORD bytes,LPOVERLAPPED request) {
    callback_count++;callback_error=error;callback_bytes=bytes;(void)request;
}
static DWORD WINAPI worker(LPVOID unused) {
    (void)unused;SetLastError(456);SetEvent(gate);return 73;
}
int halo_platform_contract(void (*check)(const char*,int)) {
    HANDLE file,event,mutex,thread;DWORD n=0,code=0;char buffer[8]={0};OVERLAPPED request;
    LARGE_INTEGER a,b,freq;int failures=0,ok;unsigned int sentinel=0x12345678;
    XINPUT_GAMEPAD pad;XINPUT_STATE live;HANDLE controller;
    WIN32_FIND_DATAA found;FILETIME stamp;
    unsigned char *allocation;
#define CHECK(name, expr) do { ok=!!(expr);check(name,ok);if(!ok)failures++; } while(0)
    halo_vita_convert_pad(0,128,128,128,128,&pad);
    CHECK("stick centers",pad.sThumbLX==0&&pad.sThumbLY==0&&pad.sThumbRX==0&&pad.sThumbRY==0);
    halo_vita_convert_pad(SCE_CTRL_CROSS|SCE_CTRL_RTRIGGER,0,0,255,255,&pad);
    CHECK("stick endpoints and Y direction",pad.sThumbLX==-32768&&pad.sThumbLY==32767&&pad.sThumbRX==32767&&pad.sThumbRY==-32767);
    CHECK("Xbox A and right trigger",pad.bAnalogButtons[XINPUT_GAMEPAD_A]==255&&pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]==255);
    allocation=GlobalReAlloc(NULL,8,GMEM_ZEROINIT);
    CHECK("allocate via GlobalReAlloc NULL",allocation&&LocalSize(allocation)==8&&allocation[0]==0&&allocation[7]==0);
    if(allocation){
        allocation[0]=42;allocation=GlobalReAlloc(allocation,16,GMEM_ZEROINIT);
        CHECK("grow preserves data and zeroes new bytes",allocation&&allocation[0]==42&&allocation[8]==0&&allocation[15]==0);
        LocalFree(allocation);
    }
    XInitDevices(0,NULL);controller=XInputOpen(XDEVICE_TYPE_GAMEPAD,0,0,NULL);
    CHECK("live Xbox controller state",controller&&XInputGetState(controller,&live)==ERROR_SUCCESS);XInputClose(controller);
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&a);Sleep(10);QueryPerformanceCounter(&b);
    CHECK("microsecond monotonic clock",freq.QuadPart==1000000&&b.QuadPart>a.QuadPart);
    event=CreateEventA(NULL,FALSE,FALSE,NULL);
    CHECK("unsignaled event poll",event&&WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    SetEvent(event);CHECK("auto-reset event consumes signal",WaitForSingleObject(event,10)==WAIT_OBJECT_0&&WaitForSingleObject(event,0)==WAIT_TIMEOUT);CloseHandle(event);
    event=CreateEventA(NULL,TRUE,TRUE,NULL);
    CHECK("manual-reset event stays signaled",event&&WaitForSingleObject(event,0)==WAIT_OBJECT_0&&WaitForSingleObject(event,0)==WAIT_OBJECT_0);
    ResetEvent(event);CHECK("manual reset",WaitForSingleObject(event,0)==WAIT_TIMEOUT);CloseHandle(event);
    mutex=CreateMutexA(NULL,FALSE,NULL);
    CHECK("recursive mutex",mutex&&WaitForSingleObject(mutex,10)==WAIT_OBJECT_0&&WaitForSingleObject(mutex,10)==WAIT_OBJECT_0&&ReleaseMutex(mutex)&&ReleaseMutex(mutex));CloseHandle(mutex);
    gate=CreateEventA(NULL,TRUE,FALSE,NULL);SetLastError(123);
    thread=CreateThread(NULL,0,worker,NULL,CREATE_SUSPENDED,NULL);
    CHECK("suspended worker",thread&&WaitForSingleObject(gate,0)==WAIT_TIMEOUT);
    CHECK("resume worker",thread&&ResumeThread(thread)==1);
    CHECK("thread completion and result",thread&&WaitForSingleObject(thread,1000)==WAIT_OBJECT_0&&GetExitCodeThread(thread,&code)&&code==73);
    CHECK("last error isolated between threads",GetLastError()==123);
    CHECK("close terminated worker",CloseHandle(thread));CloseHandle(gate);
    file=CreateFileA("z:\\platform-contract.bin",GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    CHECK("create scratch file",file!=INVALID_HANDLE_VALUE);
    if(file!=INVALID_HANDLE_VALUE) {
        CHECK("write file",WriteFile(file,"abcdef",6,&n,NULL)&&n==6);
        CHECK("file timestamp available",GetFileTime(file,&stamp,NULL,NULL)&&stamp.dwHighDateTime!=0);
        CloseHandle(file);
        file=CreateFileA("z:\\platform-contract.bin",GENERIC_WRITE|GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
        CHECK("overwrite is not append",file!=INVALID_HANDLE_VALUE&&SetFilePointer(file,2,NULL,FILE_BEGIN)==2&&WriteFile(file,"XY",2,&n,NULL));
        SetFilePointer(file,0,NULL,FILE_BEGIN);ReadFile(file,buffer,6,&n,NULL);
        CHECK("overwritten contents",n==6&&!memcmp(buffer,"abXYef",6));
        SetFilePointer(file,1,NULL,FILE_BEGIN);memset(&request,0,sizeof(request));request.Offset=2;request.hEvent=&sentinel;
        CHECK("issue Ex read",ReadFileEx(file,buffer,2,&request,completed));
        CHECK("callback deferred and hEvent ignored",callback_count==0&&sentinel==0x12345678);
        CHECK("alertable completion",SleepEx(0,TRUE)==WAIT_IO_COMPLETION&&callback_count==1&&callback_error==0&&callback_bytes==2&&!memcmp(buffer,"XY",2));
        CHECK("Ex read preserves file position",SetFilePointer(file,0,NULL,FILE_CURRENT)==1);
        CloseHandle(file);DeleteFileA("z:\\platform-contract.bin");
    }
    file=CreateFileA("d:\\maps\\ui.map",GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
    CHECK("open real ui.map through XAPI",file!=INVALID_HANDLE_VALUE);
    if(file!=INVALID_HANDLE_VALUE){CHECK("read real map header",ReadFile(file,buffer,4,&n,NULL)&&n==4&&!memcmp(buffer,"daeh",4));CloseHandle(file);}
    file=FindFirstFileA("d:\\maps\\ui.map",&found);
    CHECK("exact directory search",file!=INVALID_HANDLE_VALUE&&!strcmp(found.cFileName,"ui.map"));
    if(file!=INVALID_HANDLE_VALUE){CHECK("search does not return unrelated maps",!FindNextFileA(file,&found));CloseHandle(file);}
    return failures;
}
