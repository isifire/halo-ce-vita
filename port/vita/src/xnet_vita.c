/*
 * XNET_VITA.C
 *
 * PS Vita implementation of Xbox Winsock and XNet services.
 * Maps game networking calls (halo_ws_* and XNet*) to PS Vita's SceNet stack.
 */

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define WSAEWOULDBLOCK 10035
#define WSAEINVAL      10022
#define WSAENOTSOCK    10038

static int g_wsa_last_error = 0;
static int g_net_initialized = 0;

int WSAAPI WSAGetLastError(void)
{
	return g_wsa_last_error;
}

void WSAAPI WSASetLastError(int iError)
{
	g_wsa_last_error = iError;
}

int WSAAPI WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
{
	(void)wVersionRequested;
	if (lpWSAData)
	{
		memset(lpWSAData, 0, sizeof(*lpWSAData));
		lpWSAData->wVersion = 0x0202;
		lpWSAData->wHighVersion = 0x0202;
		strcpy(lpWSAData->szDescription, "PS Vita SceNet Winsock");
		strcpy(lpWSAData->szSystemStatus, "Running");
		lpWSAData->iMaxSockets = 256;
		lpWSAData->iMaxUdpDg = 65507;
	}
	if (!g_net_initialized)
	{
		sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		static char net_memory[256 * 1024];
		SceNetInitParam initparam;
		initparam.memory = net_memory;
		initparam.size = sizeof(net_memory);
		initparam.flags = 0;
		sceNetInit(&initparam);
		sceNetCtlInit();
		g_net_initialized = 1;
	}
	return 0;
}

int WSAAPI WSACleanup(void)
{
	return 0;
}

SOCKET WSAAPI halo_ws_socket(int af, int type, int protocol)
{
	int s = sceNetSocket("halo_sock", af, type, protocol);
	if (s < 0)
	{
		g_wsa_last_error = WSAENOTSOCK;
		return INVALID_SOCKET;
	}
	return (SOCKET)s;
}

int WSAAPI halo_ws_closesocket(SOCKET s)
{
	int ret = sceNetSocketClose((int)s);
	return ret < 0 ? SOCKET_ERROR : 0;
}

int WSAAPI halo_ws_bind(SOCKET s, const struct sockaddr *name, int namelen)
{
	int ret = sceNetBind((int)s, (const SceNetSockaddr *)name, (unsigned int)namelen);
	return ret < 0 ? SOCKET_ERROR : 0;
}

int WSAAPI halo_ws_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
	int ret = sceNetConnect((int)s, (const SceNetSockaddr *)name, (unsigned int)namelen);
	return ret < 0 ? SOCKET_ERROR : 0;
}

int WSAAPI halo_ws_listen(SOCKET s, int backlog)
{
	int ret = sceNetListen((int)s, backlog);
	return ret < 0 ? SOCKET_ERROR : 0;
}

SOCKET WSAAPI halo_ws_accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
	unsigned int len = addrlen ? (unsigned int)*addrlen : 0;
	int ret = sceNetAccept((int)s, (SceNetSockaddr *)addr, addrlen ? &len : NULL);
	if (addrlen) *addrlen = (int)len;
	return ret < 0 ? INVALID_SOCKET : (SOCKET)ret;
}

int WSAAPI halo_ws_send(SOCKET s, const char *buf, int len, int flags)
{
	return sceNetSend((int)s, buf, (unsigned int)len, flags);
}

int WSAAPI halo_ws_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
	return sceNetSendto((int)s, buf, (unsigned int)len, flags, (const SceNetSockaddr *)to, (unsigned int)tolen);
}

int WSAAPI halo_ws_recv(SOCKET s, char *buf, int len, int flags)
{
	int ret = sceNetRecv((int)s, buf, (unsigned int)len, flags);
	if (ret < 0)
	{
		g_wsa_last_error = WSAEWOULDBLOCK;
		return SOCKET_ERROR;
	}
	return ret;
}

int WSAAPI halo_ws_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
	unsigned int flen = fromlen ? (unsigned int)*fromlen : 0;
	int ret = sceNetRecvfrom((int)s, buf, (unsigned int)len, flags, (SceNetSockaddr *)from, fromlen ? &flen : NULL);
	if (fromlen) *fromlen = (int)flen;
	if (ret < 0)
	{
		g_wsa_last_error = WSAEWOULDBLOCK;
		return SOCKET_ERROR;
	}
	return ret;
}

int WSAAPI halo_ws_shutdown(SOCKET s, int how)
{
	return sceNetShutdown((int)s, how);
}

int WSAAPI halo_ws_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
	return sceNetSetsockopt((int)s, level, optname, optval, (unsigned int)optlen);
}

int WSAAPI halo_ws_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
	unsigned int len = optlen ? (unsigned int)*optlen : 0;
	int ret = sceNetGetsockopt((int)s, level, optname, optval, optlen ? &len : NULL);
	if (optlen) *optlen = (int)len;
	return ret;
}

int WSAAPI halo_ws_getsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
	unsigned int len = namelen ? (unsigned int)*namelen : 0;
	int ret = sceNetGetsockname((int)s, (SceNetSockaddr *)name, namelen ? &len : NULL);
	if (namelen) *namelen = (int)len;
	return ret;
}

int WSAAPI halo_ws_getpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
	unsigned int len = namelen ? (unsigned int)*namelen : 0;
	int ret = sceNetGetpeername((int)s, (SceNetSockaddr *)name, namelen ? &len : NULL);
	if (namelen) *namelen = (int)len;
	return ret;
}

int WSAAPI halo_ws_ioctlsocket(SOCKET s, long cmd, u_long *argp)
{
	(void)s;
	(void)cmd;
	(void)argp;
	return 0;
}

int WSAAPI halo_ws_select(int nfds, halo_ws_fd_set *readfds, halo_ws_fd_set *writefds,
	halo_ws_fd_set *exceptfds, const struct halo_ws_timeval *timeout)
{
	(void)nfds;
	(void)readfds;
	(void)writefds;
	(void)exceptfds;
	(void)timeout;
	return 0;
}

int PASCAL __WSAFDIsSet(SOCKET socket, halo_ws_fd_set *set)
{
	u_int i;
	if (!set) return 0;
	for (i = 0; i < set->fd_count; i++)
	{
		if (set->fd_array[i] == socket)
			return 1;
	}
	return 0;
}

/* ---------- Byte swapping */

u_long WSAAPI halo_ws_htonl(u_long hostlong)
{
	return __builtin_bswap32(hostlong);
}

u_long WSAAPI halo_ws_ntohl(u_long netlong)
{
	return __builtin_bswap32(netlong);
}

u_short WSAAPI halo_ws_htons(u_short hostshort)
{
	return (u_short)((hostshort << 8) | (hostshort >> 8));
}

u_short WSAAPI halo_ws_ntohs(u_short netshort)
{
	return (u_short)((netshort << 8) | (netshort >> 8));
}

unsigned long WSAAPI halo_ws_inet_addr(const char *text)
{
	unsigned long parts[4];
	int count = 0;

	if (!text) return INADDR_NONE;
	while (count < 4)
	{
		unsigned long value = 0;
		int digits = 0;

		while (*text >= '0' && *text <= '9')
		{
			value = value * 10 + (unsigned long)(*text++ - '0');
			digits++;
		}
		if (!digits || value > 255)
			return INADDR_NONE;
		parts[count++] = value;
		if (*text != '.')
			break;
		text++;
	}
	if (count != 4 || *text)
		return INADDR_NONE;
	return parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
}

/* ---------- XNet */

INT WSAAPI XNetStartup(const XNetStartupParams *params)
{
	(void)params;
	return 0;
}

INT WSAAPI XNetCleanup(void)
{
	return 0;
}

INT WSAAPI XNetRandom(BYTE *buf, UINT size)
{
	UINT i;
	for (i = 0; i < size; i++)
		buf[i] = (BYTE)(rand() & 0xFF);
	return 0;
}

INT WSAAPI XNetCreateKey(XNKID *key_id, XNKEY *key)
{
	XNetRandom((BYTE *)key_id, sizeof(*key_id));
	XNetRandom((BYTE *)key, sizeof(*key));
	return 0;
}

INT WSAAPI XNetRegisterKey(const XNKID *key_id, const XNKEY *key)
{
	(void)key_id;
	(void)key;
	return 0;
}

INT WSAAPI XNetUnregisterKey(const XNKID *key_id)
{
	(void)key_id;
	return 0;
}

INT WSAAPI XNetXnAddrToInAddr(const XNADDR *addr, const XNKID *key_id, IN_ADDR *res)
{
	(void)key_id;
	if (res && addr)
		res->s_addr = addr->ina.s_addr;
	return 0;
}

DWORD WSAAPI XNetGetTitleXnAddr(XNADDR *addr)
{
	if (!addr)
		return 0;
	memset(addr, 0, sizeof(*addr));
	addr->bSizeOfStruct = sizeof(*addr);
	addr->ina.s_addr = 0x0100007F; /* 127.0.0.1 */
	return XNET_GET_XNADDR_ETHERNET;
}

DWORD WSAAPI XNetGetEthernetLinkStatus(void)
{
	return XNET_ETHERNET_LINK_ACTIVE | XNET_ETHERNET_LINK_100MBPS | XNET_ETHERNET_LINK_FULL_DUPLEX;
}

/* ---------- Vita System / Linker Helpers */

int sceDmacMemcpy(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
	return 0;
}

int sceShaccCgExtDisableExtensions(void)
{
	return 0;
}

int sceShaccCgExtEnableExtensions(void)
{
	return 0;
}

__attribute__((weak)) void _fini(void)
{
}

__attribute__((weak)) void _free_vita_newlib(void *ptr)
{
	free(ptr);
}

int halo_linux_vprintf(const char *format, va_list args)
{
	return vprintf(format, args);
}

__attribute__((weak)) void *__dso_handle = 0;

int isfinite(double x)
{
	return __builtin_isfinite(x);
}
