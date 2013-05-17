#ifndef __bpmap_h_
#define __bpmap_h_

#include <afx.h>

// bpmap: barcode to product map

#define DEF_BPMAP_FILE "bpmap.txt"

struct SbpItem
{
	CString sBarcode;
	CString sProduct; // The string to sent to NQuire for display(actually, display command)
};

enum { MAX_ITEM = 1000 };

enum LoadfileRet_et 
{
	E_Success = 0,
	E_Unknown = -1,
	E_FileOpenFail = -2
};


class CProductList
{
public:
	CProductList();
	~CProductList();

	LoadfileRet_et LoadMapfile(const char *szfn);
	const char *GetProductByBarcode(const char *barcode);
		// return NULL if no match
	
	int Items(){ return m_items; }

private:
	LoadfileRet_et inLoadMapfile(const char *szfn);

	static int _thread_auto_load_bpmap(void *param);
	int thread_auto_load_bpmap();

	struct SAutoMutex
	{
		SAutoMutex(HANDLE m_) : m(m_) 
		{ 
			WaitForSingleObject(m, INFINITE); 
		}
		~SAutoMutex()                 
		{ 
			ReleaseMutex(m); 
		}
		HANDLE m;
	};

private:
	SbpItem *mar_items;
	int m_items;

	time_t m_tModified;
		// Recent time when bpmap.txt was modified.

	HANDLE m_hMutex;
		// internal mutex protecting mar_items[]

	HANDLE m_hQuitEvent;
		// When the main thread wants to quit, it will signal this event . 

	HANDLE m_hThread;
};




#endif

