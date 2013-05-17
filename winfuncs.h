#ifndef __winfuncs_h_20080710_
#define __winfuncs_h_20080710_

// This header provides functions that make workarounds to PC Windows & WinCE
// API difference.

#include <windows.h>

typedef int (*PROC_1sncThread)(void *param);
	// Note: This defaults to __cdecl, not __stdcall .

HANDLE winCreateThread(PROC_1sncThread proc, void *param, int stacksize=0, unsigned *pThreadId=0);


#endif
