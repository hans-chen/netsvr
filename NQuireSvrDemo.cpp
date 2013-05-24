#include <afx.h> // include MFC headers

#include <WinSock2.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

#include <process.h>
#include <commctrl.h>
#include <tchar.h>

#include <commdefs.h>

#include "fd_select.h"
#include "winfuncs.h"

//#include "bpmap.h"
#include "SendAnswer.h"

#define OUTFILE "NQuire_DISPLAY.txt"
#define INFILE "NQuire_QR.txt"

int g_nodata_timeout_sec = 0; // default: timeout infinitely
int g_cur_connections = 0;
int g_isRecvToStderr = NO;
//int g_isLogToFile = NO;

enum { idxTcpListen=0, idxUdpRead=1 };

enum { UDP_MAX_RECV_LEN = 2040 };

void 
SplitUdpTcpPort(const char *str, Ushort &udpport, Ushort &tcpport)
{
	const char *pComma = strchr(str, ',');
	if(!pComma)
		tcpport = udpport = atoi(str);
	else
	{
		udpport = atoi(str);
		tcpport = atoi(pComma+1);
	}
}

static char *
IPv4Addr2Str(const sockaddr_in &addr, char str[24])
{
	sprintf(str, "%s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	return str;
}

static char * 
GetPeerNameStr(SOCKET sock, char str[24])
{
	str[0] = '\0';

	sockaddr_in addr_peer;
	int len = sizeof(addr_peer);
	if( getpeername(sock, (sockaddr*)&addr_peer, &len) == NOERROR_0 )
	{
		IPv4Addr2Str(addr_peer, str);
	}
	return str;
}

static char * 
GetTimeStr(char *buf, int bufsize)
{
	buf[bufsize-1] = '\0'; // Do this since _snprintf does not append '\0' is buffer would overflow.
	SYSTEMTIME st;
	GetLocalTime(&st);
	_snprintf(buf, bufsize-1, "%02d-%02d %02d:%02d:%02d.%03d",
		st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, 
		st.wMilliseconds);
	return buf;
}

static
FILE *
CreateFileByTime(const SYSTEMTIME &st)
{
	char szFileName[64]; // record the accepting time.
	_snprintf(szFileName, sizeof szFileName, "cli%04d%02d%02d-%02d%02d%02d.%03d",
		st.wYear, st.wMonth, st.wDay, 
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	FILE *fp = fopen(szFileName, "wb+");
	return fp;
}

struct SEsParam
{
	SOCKET sock_conn;
//	Ulong thread_id;
	Ulong recv_bytes;
	Ulong msec_start;
};

void 
ReplaceSubstring(CString &s, const char *pszOld, const char *pszNew)
{
	// Replace occurrence of pszNew in s.
	// I do not use CString.Replace() to do the substitution, because it work in
	// "MBCS" flavor, while we want a strict byte-sequence replacement.
	// Example: On Chinese Windows XP, a UTF-8 byte stream such as
	//
	//	CString str = "\xE7\x94\xB5<03>ABC"; // E7 94 B5 is µç
	//	str.Replace("<03>", "\x03");
	//
	// will fail to get "<03>" replaced.

	int nlen = strlen(pszOld);
	for(;;)
	{
		const char *head = s;
		const char *pLoc = strstr(head, pszOld);
		if(!pLoc)
			return;
		
		s = s.Left(pLoc-head) 
			+ pszNew
			+ s.Right((const char*)s+s.GetLength() - pLoc - nlen);
	}
}

static int 
hexchar2val(char hexchar)
{
	if(hexchar<='9')
		return hexchar - '0';
	else if(hexchar>='a')
		return hexchar - 'a' + 10;
	else
		return hexchar - 'A' + 10;
}

int 
CheckHexPincer(const char *p)
{
	if(! (p[0]=='<' && p[3]=='>'))
		return -1;

	if( isxdigit(p[1]) && isxdigit(p[2]) )
	{
		return hexchar2val(p[1])*16 + hexchar2val(p[2]);
	}
	else
		return -1;
}


CString & // return just the input param 
ReplaceHexPincer(CString &s)
{
	// s will be modified
	// These substring will be considered as Hex pincer and get replaced:
	// "<80>" => '\x80'
	// "<A0>" => '\xA0'
	// "<a0>" => '\xa0'
	// And these will not be replaced:
	// "<00>"  
	// "<D>"   // only one char inside < > bracket.
	// "<K1>"  // 'K' is not a valid hexadecimal char.

	CString t;
	const int ex_3 = 3;
	int origlen = s.GetLength();
	char *ps = s.GetBuffer(origlen);
	char *pt = t.GetBuffer(origlen+ex_3);
	memcpy(pt, (const char*)s, origlen); 
	memset(pt+origlen, 0, ex_3+1); // make a copy of s into t

	// Now will copy t back to s with "<80>" etc converted to "\x80"
	int ti=0, si=0;
	for(; ti<origlen;)
	{
		int hex = CheckHexPincer(pt+ti);
		if(hex>0)
		{
			ps[si] = hex;
			ti+=4, si++;
		}
		else
		{
			ps[si] = pt[ti];
			ti++, si++;
		}
	}

	s.ReleaseBuffer(si);
	t.ReleaseBuffer(-1);
	
	return s;
}


int 
_EchoServer(void *pParam)
{
	SEsParam *pEsp = (SEsParam*)pParam;
	SOCKET sock = pEsp->sock_conn;

	SYSTEMTIME st;
	GetLocalTime(&st);

	int nRd = 0;
	char rbuf[2048];
	char tbuf[24];
	char timebuf[40];
	CString sAccumBuf;

	TcpSendAnswer tcpSA(sock);

	int isendtimeout = GetTickCount();

	for(;;)
	{
		int nRd = select_for_read(sock, 1000);
			// Use no timeout value(wait infinitely) if g_nodata_timeout_sec==0.

		if (GetTickCount() - isendtimeout > 10000)
		{
			long lSize;
			char * buffer;
			size_t result;

			isendtimeout = GetTickCount();
			FILE *fp = fopen(OUTFILE, "rb");
			if(!fp)
			{
				printf("Error Load files.\n");
				return 1;
			}

			// obtain file size:
			fseek (fp , 0 , SEEK_END);
			lSize = ftell (fp);
			rewind (fp);

			// allocate memory to contain the whole file:
			buffer = (char*) malloc (sizeof(char)*lSize);
			if (buffer == NULL) 
			{
				printf ("Memory error\n"); 
				return 2;
			}

			// copy the file into the buffer:
			result = fread (buffer, 1, lSize, fp);
			if (result != lSize) 
			{
				printf ("Reading error\n"); 
				return 3;
			}

			fclose (fp);

			int re = send(sock, (const char *)buffer, result, 0);   
			if(re==SOCKET_ERROR)
			{
				printf("Error send data.\n");
				return 4;
			}
			else
			{
				printf("Sent data.\n");
				continue;
			}

			free (buffer);
		}

		if(nRd==0)
			continue;

		nRd = recv(sock, rbuf, sizeof(rbuf)-1, 0);
			// -1: Leave one byte for adding null-terminator 
		if(nRd<=0)
		{
			g_cur_connections--;

			printf("[%d]{%s} %s closed(%d).\n", g_cur_connections, 
				GetTimeStr(timebuf, sizeof(timebuf)),
				GetPeerNameStr(sock, tbuf), nRd);
			break;
		}
		rbuf[nRd] = '\0';

		pEsp->recv_bytes += nRd;

		FILE *fp = fopen(INFILE, "wb+");
		if(!fp)
		{
			printf("Error Load files.\n");
			return 1;
		}

		fwrite(rbuf, 1, nRd, fp);
		fclose (fp);

		if(g_isRecvToStderr)
		{
			fwrite(rbuf, 1, nRd, stderr);
		}

		sAccumBuf += rbuf;

	}// for(;;)
	
	Ulong msec_elapsed = GetTickCount() - pEsp->msec_start;
/*
	printf(" recv-total: %d ", pEsp->recv_bytes);
	if(msec_elapsed>0)
	{
		printf("(%d.%03dKB/s)", pEsp->recv_bytes/msec_elapsed, 
			(pEsp->recv_bytes*1000)/msec_elapsed%1000);
	}
	else 
		printf("in 0 millisecond");

	printf(".\n");
*/
	closesocket(sock);
	
	delete pEsp;
	return 0;
}

void 
sockserv(SOCKET tcp_listen_sock, Ushort tcp_listen_port, 
			  SOCKET udp_listen_sock, Ushort udp_listen_port)
{
	int re = 0;
	char tbuf[128];
	char timebuf[40];

	sockaddr_in tcp_addr_listen = {0};
	tcp_addr_listen.sin_family = AF_INET;
	tcp_addr_listen.sin_port = htons(tcp_listen_port);

	re = bind(tcp_listen_sock, (sockaddr*)&tcp_addr_listen, sizeof(tcp_addr_listen));
	if(re!=0)
	{
		printf("TCP bind() to port %d fail!\n", tcp_listen_port);
		return;
	}
	re = listen(tcp_listen_sock, 5);
	if(re!=0)
	{
		printf("TCP listen() fail! port = %d.\n", tcp_listen_port);
		return;
	}

	sockaddr_in udp_addr_listen = {0};
	udp_addr_listen.sin_family = AF_INET;
	udp_addr_listen.sin_port = htons(udp_listen_port);

	re = bind(udp_listen_sock, (sockaddr*)&udp_addr_listen, sizeof(udp_addr_listen));
	if(re!=0)
	{
//		printf("UDP bind() to port %d fail!\n", udp_listen_port);
		return;
	}

//	printf("Start listening on UDP port %d.\n", udp_listen_port);
	printf("Start listening on TCP port %d.\n", tcp_listen_port);

	for(;;)
	{
		sockaddr_in addr_peer = {0};
		int peer_len = sizeof(addr_peer);

		// Wait accept() as well as keyboard hit
		
		SOCKET ar_sock_wait[2] = {tcp_listen_sock, udp_listen_sock};
		bool ar_sock_avai[2];

		for(;;)
		{
			int key;
			int n = select_for_reads(2, ar_sock_wait, ar_sock_avai, 1000);

			// Check key-press
			if( (key=kbhit()) != 0 )
			{
				key = getch();
				if(key==27)
					goto END;
			}

			if(n>0)
				break; // to accept
			else if(n<0) // error occurs
			{
				printf("select() error!\n");
				goto END;
			}
			else
				continue; // continue to wait
		}

		if(ar_sock_avai[idxTcpListen])
		{
			// To accept a new TCP connection

			SOCKET sock_conn = accept(tcp_listen_sock, (sockaddr*)&addr_peer, &peer_len);
			
			g_cur_connections++;
			printf("[%d]{%s} %s accepted(SOCKET=0x%X)\n", g_cur_connections,
				GetTimeStr(timebuf, sizeof(timebuf)),
				IPv4Addr2Str(addr_peer, tbuf), sock_conn);

			// Create a thread to serve the client.

			SEsParam *pEsp = new SEsParam;
			assert(pEsp);

			pEsp->sock_conn = sock_conn;
			pEsp->recv_bytes = 0;
			pEsp->msec_start = GetTickCount();

			HANDLE hThread = winCreateThread(_EchoServer, pEsp);
			if(hThread==NULL)
			{
				printf("Unexpected: Thread creation fail!\n");
				break;
			}

			CloseHandle(hThread);
		}
		else if(ar_sock_avai[idxUdpRead])
		{
			// To serve a query from UDP
	
			char str_client[24];
			char udpbuf[UDP_MAX_RECV_LEN];
			sockaddr_in addr_client;
			int len_addr = sizeof(sockaddr_in);
			int bytes_read = 0;

			bytes_read = recvfrom(udp_listen_sock, udpbuf, sizeof(udpbuf)-1, 0, 
				(sockaddr*)&addr_client, &len_addr);
	
			if(bytes_read==SOCKET_ERROR || bytes_read<0)
			{
				printf("Unexpected: recvfrom() on UDP socket fails with winerr %d.\n", GetLastError());
				continue;
			}

			udpbuf[bytes_read] = '\0';

			printf("{%s} UDP Recv %d bytes from %s\n", 
				GetTimeStr(timebuf, sizeof(timebuf)),
				bytes_read, 
				IPv4Addr2Str(addr_client, str_client));
			if(bytes_read==0) // got a NULL UDP packet
				continue;

			if(g_isRecvToStderr)
			{
				fwrite(udpbuf, 1, bytes_read, stderr);
			}

			CString sBarcode = udpbuf;
			addr_client.sin_port = htons(udp_listen_port);
				// Yes, NQuire app 1.4 expects the same receiving UDP port on both sides,
				// so we will send data to `udp_listen_port' .
			UdpSendAnswer udpSA(udp_listen_sock, addr_client);
		}
	}
END:
	return ;
}

void 
sockserv_out(Ushort tcp_listen_port, Ushort udp_listen_port)
{
	SOCKET tcp_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(tcp_listen_sock!=INVALID_SOCKET);

	SOCKET udp_listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
	assert(udp_listen_sock!=INVALID_SOCKET);

	sockserv(tcp_listen_sock, tcp_listen_port, udp_listen_sock, udp_listen_port);

	closesocket(tcp_listen_sock);
	closesocket(udp_listen_sock);
}

int 
main(int argc, char *argv[])
{
	printf("Server for morner.no\n");
	printf("Program compile date: %s %s\n", __DATE__, __TIME__);

	Ushort tcpport;

	if(argc<2)
		tcpport = 9101;
	else
		tcpport = atoi(argv[1]);

	if(tcpport==0 || tcpport>0xFFff)
	{
		printf("TCP port number %d is invalid.\n", tcpport);
		return 1;
	}

	int re;
	WSADATA wsa_data;
	re = WSAStartup(0x101, &wsa_data);
	if(re!=0)
	{
		printf("WSAStarup()=%d.\n", re);
		return 1;
	}

	printf("(Press ESC to quit.)\n");

	sockserv_out(tcpport, 1949);

	return 0;
}
