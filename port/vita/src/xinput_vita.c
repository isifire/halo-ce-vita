/* Xbox controller ABI backed by the built-in Vita controller, port zero.
 * No keyboard or memory-unit device is advertised. Vita has no rumble.
 */
#define DEBUG_KEYBOARD
#include <xtl.h>
#include <psp2/ctrl.h>
#include <string.h>

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;
static int opened, reported;
static DWORD packet;
static XINPUT_GAMEPAD previous;

static short axis(unsigned char value,int invert) {
    int delta=(int)value-128;
    int result=delta<0?delta*256:delta*32767/127;
    if(invert)result=result==-32768?32767:-result;
    return (short)result;
}
void halo_vita_convert_pad(unsigned int buttons,unsigned char lx,unsigned char ly,
    unsigned char rx,unsigned char ry,XINPUT_GAMEPAD *out) {
    memset(out,0,sizeof(*out));
    if(buttons&SCE_CTRL_UP)out->wButtons|=XINPUT_GAMEPAD_DPAD_UP;
    if(buttons&SCE_CTRL_DOWN)out->wButtons|=XINPUT_GAMEPAD_DPAD_DOWN;
    if(buttons&SCE_CTRL_LEFT)out->wButtons|=XINPUT_GAMEPAD_DPAD_LEFT;
    if(buttons&SCE_CTRL_RIGHT)out->wButtons|=XINPUT_GAMEPAD_DPAD_RIGHT;
    if(buttons&SCE_CTRL_START)out->wButtons|=XINPUT_GAMEPAD_START;
    if(buttons&SCE_CTRL_SELECT)out->wButtons|=XINPUT_GAMEPAD_BACK;
    out->bAnalogButtons[XINPUT_GAMEPAD_A]=(buttons&SCE_CTRL_CROSS)?255:0;
    out->bAnalogButtons[XINPUT_GAMEPAD_B]=(buttons&SCE_CTRL_CIRCLE)?255:0;
    out->bAnalogButtons[XINPUT_GAMEPAD_X]=(buttons&SCE_CTRL_SQUARE)?255:0;
    out->bAnalogButtons[XINPUT_GAMEPAD_Y]=(buttons&SCE_CTRL_TRIANGLE)?255:0;
    out->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]=(buttons&SCE_CTRL_LTRIGGER)?255:0;
    out->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]=(buttons&SCE_CTRL_RTRIGGER)?255:0;
    out->sThumbLX=axis(lx,0);out->sThumbLY=axis(ly,1);
    out->sThumbRX=axis(rx,0);out->sThumbRY=axis(ry,1);
}
VOID WINAPI XInitDevices(DWORD count,PXDEVICE_PREALLOC_TYPE types) {
    (void)count;(void)types;sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);reported=0;
}
BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE type,PDWORD added,PDWORD removed) {
    if(!added||!removed)return FALSE;
    *added=0;*removed=0;
    if(type==XDEVICE_TYPE_GAMEPAD&&!reported){*added=XDEVICE_PORT0_MASK;reported=1;}
    return *added!=0;
}
HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE type,DWORD port,DWORD slot,PXINPUT_POLLING_PARAMETERS params) {
    (void)slot;(void)params;
    if(type!=XDEVICE_TYPE_GAMEPAD||port!=0){SetLastError(ERROR_DEVICE_NOT_CONNECTED);return NULL;}
    opened=1;packet=0;memset(&previous,0,sizeof(previous));return &opened;
}
VOID WINAPI XInputClose(HANDLE handle){if(handle==&opened)opened=0;}
DWORD WINAPI XInputGetState(HANDLE handle,PXINPUT_STATE state) {
    SceCtrlData pad;
    if(!state)return ERROR_INVALID_PARAMETER;
    memset(state,0,sizeof(*state));
    if(handle!=&opened||!opened)return ERROR_DEVICE_NOT_CONNECTED;
    if(sceCtrlPeekBufferPositive(0,&pad,1)<=0)return ERROR_DEVICE_NOT_CONNECTED;
    halo_vita_convert_pad(pad.buttons,pad.lx,pad.ly,pad.rx,pad.ry,&state->Gamepad);
    if(memcmp(&previous,&state->Gamepad,sizeof(previous))){packet++;previous=state->Gamepad;}
    state->dwPacketNumber=packet;return ERROR_SUCCESS;
}
DWORD WINAPI XInputSetState(HANDLE handle,PXINPUT_FEEDBACK feedback) {
    if(!feedback)return ERROR_INVALID_PARAMETER;
    feedback->Header.dwStatus=handle==&opened&&opened?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
    if(feedback->Header.hEvent)SetEvent(feedback->Header.hEvent);
    return feedback->Header.dwStatus;
}
DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS p){(void)p;return ERROR_SUCCESS;}
DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE p){if(p)memset(p,0,sizeof(*p));return ERROR_HANDLE_EOF;}
int halo_linux_mouse_look(short port,float *yaw,float *pitch){(void)port;*yaw=0;*pitch=0;return 0;}
