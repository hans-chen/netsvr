#ifndef WINCE
#include <process.h> // For Desktop Windows, _beginthreadex
#endif

#include "winfuncs.h"

struct SwThreadParam
{
	PROC_1sncThread proc;
	void *param;
};

#ifdef WINCE
static DWORD				// WinCE
#else 
static unsigned __stdcall	// (PC Windows below)
#endif // #ifdef WINCE / else
_WinThreadWrapper(void * param)
{
	SwThreadParam *pw = (SwThreadParam*)param;

	int ret = pw->proc(pw->param);
	delete pw;

	return ret;
}


HANDLE 
winCreateThread(PROC_1sncThread proc, void *param, int stacksize, unsigned *pThreadId)
{
	HANDLE hThread = NULL;
	SwThreadParam *pwp = new SwThreadParam;
	if(!pwp)
		return NULL;
	
	pwp->proc = proc, pwp->param = param;

#ifdef WINCE
	DWORD tid;
	hThread = CreateThread(NULL, stacksize, _WinThreadWrapper, pwp, 
		stacksize>0 ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, &tid);
#else
	// PC Windows
	unsigned tid;
	hThread = (HANDLE)_beginthreadex(NULL, stacksize>0 ? stacksize : 64000 ,
		_WinThreadWrapper, pwp ,0, &tid);
#endif

	if(pThreadId)
		*pThreadId = tid;

	return hThread;
}

