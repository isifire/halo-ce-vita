/* Minimal Win32/XAPI platform services backed by VitaSDK.
 *
 * This layer is compiled with the game's ARM/Xbox ABI.  It intentionally
 * covers the synchronous primitives needed by cache_files and main_loop
 * bring-up first; networking, save-game enumeration and Bink remain separate.
 */
#include <xtl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/error.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Native platform paths and formatting must bypass game CRT redirections. */
#undef fopen
#undef fprintf
#undef snprintf

#define VITA_HANDLE_MAGIC 0x48564954u
#define VITA_PATH_MAX 512
#ifndef ERROR_CALL_NOT_IMPLEMENTED
#define ERROR_CALL_NOT_IMPLEMENTED 120L
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

enum vita_handle_kind { VITA_FILE=1, VITA_DIRECTORY, VITA_EVENT, VITA_MUTEX, VITA_THREAD };
struct vita_handle {
    unsigned int magic;
    int kind;
    SceUID uid;
    FILE *file;
    char path[VITA_PATH_MAX];
    int manual_reset;
    DWORD exit_code;
    LPTHREAD_START_ROUTINE thread_start;
    LPVOID thread_parameter;
    int thread_started;
};

struct completion {
    struct completion *next;
    LPOVERLAPPED request;
    LPOVERLAPPED_COMPLETION_ROUTINE callback;
};
/* One context per calling thread; callbacks only run on their issuing thread. */
struct thread_context {
    SceUID owner;
    DWORD error;
    struct completion *head, *tail;
};
static struct thread_context contexts[64];
static struct thread_context *context(void) {
    SceUID id=sceKernelGetThreadId();int i;
    for(i=0;i<64;i++) {
        if(contexts[i].owner==id)return &contexts[i];
        if(__sync_bool_compare_and_swap(&contexts[i].owner,0,id))return &contexts[i];
    }
    /* Exhaustion cannot silently dispatch another thread's callbacks. */
    fprintf(stderr,"Halo platform: thread context limit exceeded\n");abort();
}
DWORD WINAPI GetLastError(void) { return context()->error; }
void WINAPI SetLastError(DWORD error) { context()->error=error; }
static int dispatch_completions(void) {
    struct thread_context *c=context();int count=0;
    while(c->head) {
        struct completion *p=c->head;c->head=p->next;if(!c->head)c->tail=NULL;
        p->callback((DWORD)p->request->Internal,(DWORD)p->request->InternalHigh,p->request);
        free(p);count++;
    }
    return count;
}

static struct vita_handle *new_handle(int kind) {
    struct vita_handle *h=calloc(1,sizeof(*h));
    if(h){h->magic=VITA_HANDLE_MAGIC;h->kind=kind;h->uid=-1;}
    else SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return h;
}
static struct vita_handle *get_handle(HANDLE value,int kind) {
    struct vita_handle *h=(struct vita_handle*)value;
    if(!h||value==INVALID_HANDLE_VALUE||h->magic!=VITA_HANDLE_MAGIC||(kind&&h->kind!=kind)){
        SetLastError(ERROR_INVALID_HANDLE);return NULL;
    }
    return h;
}

static void translate_path(const char *guest_path,char *vita,unsigned int size) {
    char save_root[48];const char *tail=guest_path;const char *root="ux0:data/halo";
    if(guest_path&&guest_path[0]&&guest_path[1]==':'){
        char drive=(char)(guest_path[0]|32);tail=guest_path+2;
        if(drive!='d'){
            snprintf(save_root,sizeof(save_root),"ux0:data/halo-vita/%c",drive);
            sceIoMkdir("ux0:data/halo-vita",0777);sceIoMkdir(save_root,0777);root=save_root;
        }
    }
    while(*tail=='/'||*tail=='\\')tail++;
    snprintf(vita,size,"%s/%s",root,tail);
    for(char *p=vita;*p;p++)if(*p=='\\')*p='/';
}

HANDLE WINAPI CreateFileA(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,
 DWORD disposition,DWORD attributes,HANDLE template_file) {
    char path[VITA_PATH_MAX];const char *mode="rb";struct vita_handle *h;SceIoStat stat;int exists;
    (void)share;(void)security;(void)attributes;(void)template_file;
    if(!name){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
    translate_path(name,path,sizeof(path));exists=sceIoGetstat(path,&stat)>=0;
    if(disposition<CREATE_NEW||disposition>TRUNCATE_EXISTING){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
    if(disposition==CREATE_NEW&&exists){SetLastError(ERROR_ALREADY_EXISTS);return INVALID_HANDLE_VALUE;}
    if((disposition==OPEN_EXISTING||disposition==TRUNCATE_EXISTING)&&!exists){SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_HANDLE_VALUE;}
    if((disposition==TRUNCATE_EXISTING||disposition==CREATE_ALWAYS||disposition==CREATE_NEW)&&!(access&GENERIC_WRITE)){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    if(disposition==CREATE_ALWAYS)mode=(access&GENERIC_READ)?"w+b":"wb";
    else if(disposition==CREATE_NEW)mode="w+b";
    else if(disposition==OPEN_ALWAYS){mode=exists?((access&GENERIC_WRITE)?"r+b":"rb"):"w+b";}
    else if(disposition==TRUNCATE_EXISTING)mode=(access&GENERIC_READ)?"w+b":"wb";
    else if(access&GENERIC_WRITE)mode="r+b";
    h=new_handle(VITA_FILE);if(!h)return INVALID_HANDLE_VALUE;
    h->file=fopen(path,mode);
    snprintf(h->path,sizeof(h->path),"%s",path);
    if(!h->file){free(h);SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_HANDLE_VALUE;}
    SetLastError(exists&&(disposition==OPEN_ALWAYS||disposition==CREATE_ALWAYS)?ERROR_ALREADY_EXISTS:ERROR_SUCCESS);return h;
}
BOOL WINAPI CloseHandle(HANDLE value) {
    struct vita_handle *h=get_handle(value,0);if(!h)return FALSE;
    if(h->kind==VITA_FILE&&h->file)fclose(h->file);
    else if(h->kind==VITA_DIRECTORY&&h->uid>=0)sceIoDclose(h->uid);
    else if(h->kind==VITA_EVENT&&h->uid>=0)sceKernelDeleteEventFlag(h->uid);
    else if(h->kind==VITA_MUTEX&&h->uid>=0)sceKernelDeleteMutex(h->uid);
    else if(h->kind==VITA_THREAD&&h->uid>=0){
        /* Until detached lifetime ownership is implemented, reject closing a
         * running thread instead of freeing the record under its entry point. */
        if(sceKernelDeleteThread(h->uid)<0){SetLastError(ERROR_BUSY);return FALSE;}
    }
    h->magic=0;free(h);return TRUE;
}
static unsigned long long request_offset(LPOVERLAPPED o){return ((unsigned long long)o->OffsetHigh<<32)|o->Offset;}
BOOL WINAPI ReadFile(HANDLE value,LPVOID buffer,DWORD count,LPDWORD done,LPOVERLAPPED o) {
    struct vita_handle *h=get_handle(value,VITA_FILE);size_t n;if(done)*done=0;if(!h)return FALSE;
    if(o&&fseek(h->file,(long)request_offset(o),SEEK_SET)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    n=fread(buffer,1,count,h->file);if(done)*done=(DWORD)n;
    if(o){o->Internal=ferror(h->file)?ERROR_GEN_FAILURE:ERROR_SUCCESS;o->InternalHigh=n;if(o->hEvent)SetEvent(o->hEvent);}
    return ferror(h->file)?FALSE:TRUE;
}
BOOL WINAPI WriteFile(HANDLE value,LPCVOID buffer,DWORD count,LPDWORD done,LPOVERLAPPED o) {
    struct vita_handle *h=get_handle(value,VITA_FILE);size_t n;if(done)*done=0;if(!h)return FALSE;
    if(o&&fseek(h->file,(long)request_offset(o),SEEK_SET)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    n=fwrite(buffer,1,count,h->file);if(done)*done=(DWORD)n;
    if(o){o->Internal=n==count?ERROR_SUCCESS:ERROR_GEN_FAILURE;o->InternalHigh=n;if(o->hEvent)SetEvent(o->hEvent);}
    return n==count;
}
static BOOL file_ex(HANDLE value,void *buffer,DWORD count,LPOVERLAPPED o,LPOVERLAPPED_COMPLETION_ROUTINE cb,int writing){
    struct vita_handle *h=get_handle(value,VITA_FILE);struct completion *p;struct thread_context *c;
    long saved;size_t n;
    if(!h)return FALSE;
    if(!o||!cb||o->OffsetHigh||o->Offset>0x7fffffffUL){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    p=calloc(1,sizeof(*p));if(!p){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;}
    saved=ftell(h->file);clearerr(h->file);
    if(saved<0||fseek(h->file,(long)o->Offset,SEEK_SET)){free(p);SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    n=writing?fwrite(buffer,1,count,h->file):fread(buffer,1,count,h->file);
    o->Internal=(ferror(h->file)||(writing&&n!=count))?ERROR_GEN_FAILURE:(!writing&&n==0&&count?ERROR_HANDLE_EOF:ERROR_SUCCESS);
    o->InternalHigh=n;
    if(fseek(h->file,saved,SEEK_SET))o->Internal=ERROR_GEN_FAILURE;
    /* Ex APIs ignore hEvent: Halo stores a boolean pointer there, not a handle. */
    p->request=o;p->callback=cb;c=context();if(c->tail)c->tail->next=p;else c->head=p;c->tail=p;
    return TRUE;
}
BOOL WINAPI ReadFileEx(HANDLE h,LPVOID b,DWORD c,LPOVERLAPPED o,LPOVERLAPPED_COMPLETION_ROUTINE cb){return file_ex(h,b,c,o,cb,0);}
BOOL WINAPI WriteFileEx(HANDLE h,LPCVOID b,DWORD c,LPOVERLAPPED o,LPOVERLAPPED_COMPLETION_ROUTINE cb){return file_ex(h,(void*)b,c,o,cb,1);}
DWORD WINAPI SetFilePointer(HANDLE value,LONG low,PLONG high,DWORD method){
    struct vita_handle *h=get_handle(value,VITA_FILE);long offset=low;int origin;
    if(!h)return INVALID_SET_FILE_POINTER;
    if(method>FILE_END||(high&&*high!=0)){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_SET_FILE_POINTER;}
    origin=method==FILE_BEGIN?SEEK_SET:method==FILE_CURRENT?SEEK_CUR:SEEK_END;
    if(fseek(h->file,offset,origin)){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_SET_FILE_POINTER;}
    offset=ftell(h->file);if(high)*high=0;return (DWORD)offset;
}
DWORD WINAPI GetFileSize(HANDLE value,LPDWORD high){
    struct vita_handle *h=get_handle(value,VITA_FILE);long here,end;if(!h)return INVALID_FILE_SIZE;
    here=ftell(h->file);fseek(h->file,0,SEEK_END);end=ftell(h->file);fseek(h->file,here,SEEK_SET);
    if(high)*high=0;return (DWORD)end;
}
BOOL WINAPI DeleteFileA(LPCSTR name){char p[VITA_PATH_MAX];translate_path(name,p,sizeof(p));return sceIoRemove(p)>=0;}
BOOL WINAPI CreateDirectoryA(LPCSTR name,LPSECURITY_ATTRIBUTES unused){char p[VITA_PATH_MAX];(void)unused;translate_path(name,p,sizeof(p));return sceIoMkdir(p,0777)>=0;}
BOOL WINAPI RemoveDirectoryA(LPCSTR name){char p[VITA_PATH_MAX];translate_path(name,p,sizeof(p));return sceIoRmdir(p)>=0;}
DWORD WINAPI GetFileAttributesA(LPCSTR name){char p[VITA_PATH_MAX];SceIoStat s;translate_path(name,p,sizeof(p));if(sceIoGetstat(p,&s)<0)return INVALID_FILE_ATTRIBUTES;return SCE_S_ISDIR(s.st_mode)?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;}
BOOL WINAPI GetFileAttributesExA(LPCSTR name,GET_FILEEX_INFO_LEVELS level,LPVOID output){
    char p[VITA_PATH_MAX];SceIoStat s;WIN32_FILE_ATTRIBUTE_DATA*d=output;(void)level;translate_path(name,p,sizeof(p));if(!d||sceIoGetstat(p,&s)<0)return FALSE;memset(d,0,sizeof(*d));d->dwFileAttributes=SCE_S_ISDIR(s.st_mode)?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;d->nFileSizeLow=(DWORD)s.st_size;d->nFileSizeHigh=(DWORD)((unsigned long long)s.st_size>>32);return TRUE;
}
BOOL WINAPI SetFileAttributesA(LPCSTR name,DWORD attributes){(void)name;(void)attributes;SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
static int to_filetime(const SceDateTime *date,FILETIME *out){
    SceUInt64 value;if(!out)return 1;
    if(sceRtcGetWin32FileTime(date,&value)<0)return 0;
    out->dwLowDateTime=(DWORD)value;out->dwHighDateTime=(DWORD)(value>>32);return 1;
}
static int from_filetime(const FILETIME *in,SceDateTime *date){
    return sceRtcSetWin32FileTime(date,((SceUInt64)in->dwHighDateTime<<32)|in->dwLowDateTime)>=0;
}
BOOL WINAPI GetFileTime(HANDLE value,LPFILETIME created,LPFILETIME accessed,LPFILETIME written){
    struct vita_handle *h=get_handle(value,VITA_FILE);SceIoStat s;if(!h)return FALSE;
    if(sceIoGetstat(h->path,&s)<0){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
    return to_filetime(&s.st_ctime,created)&&to_filetime(&s.st_atime,accessed)&&to_filetime(&s.st_mtime,written);
}
BOOL WINAPI SetFileTime(HANDLE value,CONST FILETIME *created,CONST FILETIME *accessed,CONST FILETIME *written){
    struct vita_handle *h=get_handle(value,VITA_FILE);SceIoStat s;int bits=0;if(!h)return FALSE;
    memset(&s,0,sizeof(s));
    if(created){if(!from_filetime(created,&s.st_ctime))return FALSE;bits|=SCE_CST_CT;}
    if(accessed){if(!from_filetime(accessed,&s.st_atime))return FALSE;bits|=SCE_CST_AT;}
    if(written){if(!from_filetime(written,&s.st_mtime))return FALSE;bits|=SCE_CST_MT;}
    if(fflush(h->file)||sceIoChstat(h->path,&s,bits)<0){SetLastError(ERROR_GEN_FAILURE);return FALSE;}return TRUE;
}

static int pattern_matches(const char *pattern,const char *name){
    const char *star=NULL,*retry=NULL;
    if(!strcmp(pattern,"*.*"))pattern="*";
    while(*name){
        if(*pattern=='?'||((*pattern|32)==(*name|32)&&*pattern)){pattern++;name++;}
        else if(*pattern=='*'){star=pattern++;retry=name;}
        else if(star){pattern=star+1;name=++retry;}
        else return 0;
    }
    while(*pattern=='*')pattern++;
    return !*pattern;
}
static BOOL read_directory_entry(struct vita_handle *h,LPWIN32_FIND_DATAA output){
    SceIoDirent entry;int rc;if(!h||!output)return FALSE;
    for(;;){
        memset(&entry,0,sizeof(entry));rc=sceIoDread(h->uid,&entry);
        if(rc<=0){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
        if(strcmp(entry.d_name,".")&&strcmp(entry.d_name,"..")&&pattern_matches(h->path,entry.d_name))break;
    }
    memset(output,0,sizeof(*output));
    output->dwFileAttributes=SCE_S_ISDIR(entry.d_stat.st_mode)?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;
    output->nFileSizeLow=(DWORD)entry.d_stat.st_size;output->nFileSizeHigh=(DWORD)((unsigned long long)entry.d_stat.st_size>>32);
    to_filetime(&entry.d_stat.st_ctime,&output->ftCreationTime);
    to_filetime(&entry.d_stat.st_atime,&output->ftLastAccessTime);
    to_filetime(&entry.d_stat.st_mtime,&output->ftLastWriteTime);
    snprintf(output->cFileName,sizeof(output->cFileName),"%s",entry.d_name);return TRUE;
}
HANDLE WINAPI FindFirstFileA(LPCSTR pattern,LPWIN32_FIND_DATAA output){
    char path[VITA_PATH_MAX],*slash;struct vita_handle *h;
    if(!pattern||!output){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
    translate_path(pattern,path,sizeof(path));slash=strrchr(path,'/');
    if(!slash)return INVALID_HANDLE_VALUE;
    h=new_handle(VITA_DIRECTORY);if(!h)return INVALID_HANDLE_VALUE;
    snprintf(h->path,sizeof(h->path),"%s",slash+1);*slash=0;h->uid=sceIoDopen(path);
    if(h->uid<0||!read_directory_entry(h,output)){if(h->uid>=0)sceIoDclose(h->uid);free(h);return INVALID_HANDLE_VALUE;}return h;
}
BOOL WINAPI FindNextFileA(HANDLE value,LPWIN32_FIND_DATAA output){return read_directory_entry(get_handle(value,VITA_DIRECTORY),output);}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES unused,BOOL manual,BOOL initial,LPCSTR name){
    struct vita_handle*h=new_handle(VITA_EVENT);(void)unused;if(!h)return NULL;h->manual_reset=manual;
    h->uid=sceKernelCreateEventFlag(name?name:"halo-event",SCE_EVENT_WAITMULTIPLE,initial?1:0,NULL);if(h->uid<0){free(h);return NULL;}return h;
}
BOOL WINAPI SetEvent(HANDLE value){struct vita_handle*h=get_handle(value,VITA_EVENT);return h&&sceKernelSetEventFlag(h->uid,1)>=0;}
BOOL WINAPI ResetEvent(HANDLE value){struct vita_handle*h=get_handle(value,VITA_EVENT);return h&&sceKernelClearEventFlag(h->uid,0)>=0;}
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES unused,BOOL owner,LPCSTR name){
    struct vita_handle*h=new_handle(VITA_MUTEX);(void)unused;if(!h)return NULL;
    h->uid=sceKernelCreateMutex(name?name:"halo-mutex",SCE_KERNEL_MUTEX_ATTR_RECURSIVE,owner?1:0,NULL);if(h->uid<0){free(h);return NULL;}return h;
}
BOOL WINAPI ReleaseMutex(HANDLE value){struct vita_handle*h=get_handle(value,VITA_MUTEX);return h&&sceKernelUnlockMutex(h->uid,1)>=0;}
DWORD WINAPI WaitForSingleObjectEx(HANDLE value,DWORD ms,BOOL alertable){
    struct vita_handle*h;unsigned int bits=0;unsigned int timeout=ms>4294967?0xffffffffu:ms*1000;unsigned int *tp=ms==INFINITE?NULL:&timeout;int rc;
    if(alertable&&dispatch_completions())return WAIT_IO_COMPLETION;
    h=get_handle(value,0);if(!h)return WAIT_FAILED;
    if(h->kind==VITA_EVENT)rc=ms==0?sceKernelPollEventFlag(h->uid,1,SCE_EVENT_WAITOR|(h->manual_reset?0:SCE_EVENT_WAITCLEAR),&bits):sceKernelWaitEventFlag(h->uid,1,SCE_EVENT_WAITOR|(h->manual_reset?0:SCE_EVENT_WAITCLEAR),&bits,tp);
    else if(h->kind==VITA_MUTEX)rc=ms==0?sceKernelTryLockMutex(h->uid,1):sceKernelLockMutex(h->uid,1,tp);
    else if(h->kind==VITA_THREAD)rc=sceKernelWaitThreadEnd(h->uid,NULL,tp);
    else return WAIT_OBJECT_0;
    if(rc>=0)return WAIT_OBJECT_0;
    if(rc==(int)SCE_KERNEL_ERROR_WAIT_TIMEOUT||rc==(int)SCE_KERNEL_ERROR_EVF_COND||rc==(int)SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN)return WAIT_TIMEOUT;
    SetLastError(ERROR_GEN_FAILURE);return WAIT_FAILED;
}
DWORD WINAPI WaitForSingleObject(HANDLE h,DWORD ms){return WaitForSingleObjectEx(h,ms,FALSE);}
VOID WINAPI Sleep(DWORD ms){if(ms==INFINITE){for(;;)sceKernelDelayThread(1000000);}while(ms>1000){sceKernelDelayThread(1000000);ms-=1000;}sceKernelDelayThread(ms?ms*1000:1);}
DWORD WINAPI SleepEx(DWORD ms,BOOL alertable){if(alertable&&dispatch_completions())return WAIT_IO_COMPLETION;Sleep(ms);return 0;}
BOOL WINAPI SwitchToThread(void){sceKernelDelayThread(0);return TRUE;}

static int vita_thread_entry(SceSize argc,void *argument){struct vita_handle*h=argc>=sizeof(h)?*(struct vita_handle**)argument:NULL;DWORD result=0;if(h&&h->thread_start)result=h->thread_start(h->thread_parameter);if(h)h->exit_code=result;return (int)result;}
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES unused,DWORD stack,LPTHREAD_START_ROUTINE start,LPVOID parameter,DWORD flags,LPDWORD thread_id){
    struct vita_handle*h=new_handle(VITA_THREAD);(void)unused;if(!h)return NULL;h->thread_start=start;h->thread_parameter=parameter;h->exit_code=STILL_ACTIVE;h->uid=sceKernelCreateThread("halo-thread",vita_thread_entry,0x10000100,stack<0x10000?0x10000:stack,0,0,NULL);if(h->uid<0){free(h);return NULL;}if(thread_id)*thread_id=(DWORD)h->uid;if(!(flags&CREATE_SUSPENDED)){struct vita_handle*copy=h;if(sceKernelStartThread(h->uid,sizeof(copy),&copy)<0){sceKernelDeleteThread(h->uid);free(h);return NULL;}h->thread_started=1;}return h;
}
DWORD WINAPI ResumeThread(HANDLE value){struct vita_handle*h=get_handle(value,VITA_THREAD);struct vita_handle*copy;if(!h)return (DWORD)-1;if(h->thread_started)return 0;copy=h;if(sceKernelStartThread(h->uid,sizeof(copy),&copy)<0)return (DWORD)-1;h->thread_started=1;return 1;}
BOOL WINAPI GetExitCodeThread(HANDLE value,LPDWORD code){struct vita_handle*h=get_handle(value,VITA_THREAD);int status;if(!h||!code)return FALSE;if(sceKernelGetThreadExitStatus(h->uid,&status)>=0)h->exit_code=(DWORD)status;*code=h->exit_code;return TRUE;}
BOOL WINAPI SetThreadPriority(HANDLE value,int priority){struct vita_handle*h=get_handle(value,VITA_THREAD);if(!h)return FALSE;(void)priority;return TRUE;}

DWORD WINAPI GetTickCount(void){return (DWORD)(sceKernelGetProcessTimeWide()/1000);}
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *v){v->QuadPart=(LONGLONG)sceKernelGetProcessTimeWide();return TRUE;}
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *v){v->QuadPart=1000000;return TRUE;}

#define FILETIME_EPOCH 11644473600ULL
static unsigned long long filetime_value(const FILETIME*t){return ((unsigned long long)t->dwHighDateTime<<32)|t->dwLowDateTime;}
LONG WINAPI CompareFileTime(CONST FILETIME*a,CONST FILETIME*b){unsigned long long x=filetime_value(a),y=filetime_value(b);return x<y?-1:x>y?1:0;}
BOOL WINAPI SystemTimeToFileTime(CONST SYSTEMTIME*s,LPFILETIME f){
    struct tm t;time_t unix_time;unsigned long long value;memset(&t,0,sizeof(t));t.tm_year=s->wYear-1900;t.tm_mon=s->wMonth-1;t.tm_mday=s->wDay;t.tm_hour=s->wHour;t.tm_min=s->wMinute;t.tm_sec=s->wSecond;unix_time=mktime(&t);if(unix_time<0)return FALSE;value=((unsigned long long)unix_time+FILETIME_EPOCH)*10000000ULL+s->wMilliseconds*10000ULL;f->dwLowDateTime=(DWORD)value;f->dwHighDateTime=(DWORD)(value>>32);return TRUE;
}
VOID WINAPI GetSystemTime(LPSYSTEMTIME s){time_t now=time(NULL);struct tm*t=gmtime(&now);memset(s,0,sizeof(*s));if(t){s->wYear=t->tm_year+1900;s->wMonth=t->tm_mon+1;s->wDay=t->tm_mday;s->wDayOfWeek=t->tm_wday;s->wHour=t->tm_hour;s->wMinute=t->tm_min;s->wSecond=t->tm_sec;}}

BOOL WINAPI VirtualProtect(LPVOID address,SIZE_T size,DWORD protect,PDWORD old){(void)address;(void)size;(void)old;(void)protect;SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
VOID WINAPI OutputDebugStringA(LPCSTR text){if(text)fprintf(stderr,"%s",text);}

struct global_block {SIZE_T size;unsigned char data[1];};
HGLOBAL WINAPI GlobalAlloc(UINT flags,SIZE_T size){
    struct global_block *b;
    if(size>(SIZE_T)-1-sizeof(*b)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    b=malloc(sizeof(*b)+size);if(!b)return NULL;
    b->size=size;if(flags&GMEM_ZEROINIT)memset(b->data,0,size);return b->data;
}
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL value,SIZE_T size,UINT flags){
    struct global_block *block;SIZE_T old_size;
    if(!value)return GlobalAlloc(flags,size);
    if(size>(SIZE_T)-1-sizeof(*block)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    block=(struct global_block*)((char*)value - offsetof(struct global_block,data));old_size=block->size;
    block=realloc(block,sizeof(*block)+size);if(!block)return NULL;
    if((flags&GMEM_ZEROINIT)&&size>old_size)memset(block->data+old_size,0,size-old_size);
    block->size=size;return block->data;
}
HLOCAL WINAPI LocalFree(HLOCAL value){if(value)free((char*)value-offsetof(struct global_block,data));return NULL;}
SIZE_T WINAPI LocalSize(HLOCAL value){struct global_block*b=value?(struct global_block*)((char*)value-offsetof(struct global_block,data)):NULL;return b?b->size:0;}
