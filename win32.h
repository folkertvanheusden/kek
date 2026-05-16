#pragma once

#include <cstdarg>

int vasprintf(char **strp, const char *format, va_list ap);
int asprintf(char **strp, const char *format, ...);

// from https://stackoverflow.com/questions/12765743/implicit-declaration-of-function-getaddrinfo-on-mingw
#define _NTDDI_VERSION_FROM_WIN32_WINNT2(ver)    ver##0000
#define _NTDDI_VERSION_FROM_WIN32_WINNT(ver)     _NTDDI_VERSION_FROM_WIN32_WINNT2(ver)

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x501
#endif
#ifndef NTDDI_VERSION
#  define NTDDI_VERSION _NTDDI_VERSION_FROM_WIN32_WINNT(_WIN32_WINNT)
#endif

#include <ws2tcpip.h>
#include <winsock2.h>

/*
typedef struct pollfd {
	SOCKET fd;
	SHORT events;
	SHORT revents;
} WSAPOLLFD, *PWSAPOLLFD, FAR *LPWSAPOLLFD;
WINSOCK_API_LINKAGE int WSAAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, INT timeout);
*/
