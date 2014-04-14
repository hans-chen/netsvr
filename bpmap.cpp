#define _USE_32BIT_TIME_T
#include <sys/types.h>
#include <sys/stat.h>

#include "bpmap.h"
#include "winfuncs.h"

#define BUFSIZE 4000

static TCHAR s_szNullProductText[BUFSIZE];
	// When an input barcode is not found in the mapping file, use this text.
static LPCTSTR s_pszNullProductText = s_szNullProductText;

CProductList::CProductList() :
	mar_items(NULL), m_items(0), 
	m_tModified(0),
	m_hMutex(NULL), m_hQuitEvent(NULL), m_hThread(NULL)
{
	mar_items = new SbpItem[MAX_ITEM];

	m_hMutex = CreateMutex(NULL, FALSE, NULL);

	m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

CProductList::~CProductList()
{
	delete []mar_items;

	SetEvent(m_hQuitEvent); // tell monitor thread to quit
	WaitForSingleObject(m_hThread, INFINITE);

	CloseHandle(m_hMutex);
	CloseHandle(m_hQuitEvent);

	CloseHandle(m_hThread);
}

LoadfileRet_et
CProductList::inLoadMapfile(LPCTSTR szfn)
{
	SAutoMutex am(m_hMutex);

	char s[BUFSIZE]; //, *sptr = NULL;
	int ret = 0;

	m_items = 0;

	struct _stat32 filest;
	int err = _wstat(szfn, &filest);
	if(!err)
	{
		m_tModified = filest.st_mtime;
	}

	FILE *fp = _wfopen(szfn, _T("rb"));
	if(!fp)
		return E_FileOpenFail;

	for(;;)
	{
		ret = fscanf(fp, "%s", s);
		if(ret==0 || ret==EOF)
			break; // reached end of file

		// special >>>
		if(strcmp(s, "*")==0)
		{
			fgetws(s_szNullProductText, sizeof(s_szNullProductText), fp);
			s_pszNullProductText = s_szNullProductText;
			while(*s_pszNullProductText==' ' || *s_pszNullProductText=='\t') 
				s_pszNullProductText++; // trim leading space and tabs
			continue;
		}
		// special <<<

		mar_items[m_items].sBarcode = s;

		fgets(s, sizeof(s), fp);

		mar_items[m_items].sProduct = s;
		mar_items[m_items].sProduct.TrimLeft();
		mar_items[m_items].sProduct.TrimRight(_T("\r\n"));

		m_items++;
	}

	fclose(fp);
	return E_Success;
}

int CProductList::_thread_auto_load_bpmap(void *param)
{
	return ((CProductList*)param)->thread_auto_load_bpmap();
}

int CProductList::thread_auto_load_bpmap()
{
	LoadfileRet_et lferr = E_Unknown;
	int err = 0;
	struct _stat32 filest;
	for(;;)
	{
		BOOL b = WaitForSingleObject(m_hQuitEvent, 1000);
		if(b==WAIT_OBJECT_0)
			break;

		err = _wstat(DEF_BPMAP_FILE, &filest);
		if(err)
		{
			// maybe the file has been deleted.
			continue;
		}

		if(filest.st_mtime!=m_tModified)
		{	// time different, now we try to reload the file.
			lferr = inLoadMapfile(DEF_BPMAP_FILE);
			if(!lferr)
			{
				fprintf(stderr, 
					"INFO: %s change detected and reloaded (%d items in list).\n", 
					DEF_BPMAP_FILE, 
					Items()
					);
			}
			else
			{
				fprintf(stderr, "WARNING: %s change detected but reload error(will try later).\n", DEF_BPMAP_FILE);
			}
		}
	}

	return 0;
}


LoadfileRet_et
CProductList::LoadMapfile(LPCTSTR szfn)
{
	LoadfileRet_et lferr = inLoadMapfile(szfn);
	if(lferr)
		return lferr;

	m_hThread = winCreateThread(_thread_auto_load_bpmap, this);
	if(!m_hThread)
	{
		fprintf(stderr, "Fail to create %s monitor thread. "
			"You can go on, but background change of %s will not cause it to be reloaded.\n", 
			DEF_BPMAP_FILE, DEF_BPMAP_FILE);
	}

	return E_Success;
}

LPCTSTR 
CProductList::GetProductByBarcode(LPCTSTR barcode)
{
	SAutoMutex am(m_hMutex);

	int i;
	for(i=0; i<m_items; i++)
	{
		if(mar_items[i].sBarcode==barcode)
			return mar_items[i].sProduct;
	}

	if(s_szNullProductText[0])
		return s_pszNullProductText;
	else
		return NULL;
}
