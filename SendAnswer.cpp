#include <WinSock2.h>

#include "SendAnswer.h"

TcpSendAnswer::TcpSendAnswer(SOCKET s)
{
	m_sock = s;
}

ISendAnswer::Recode_et
TcpSendAnswer::SendAnswer(const void *pSend, int bytes)
{
	int re = send(m_sock, (const char *)pSend, bytes, 0);   
	if(re==SOCKET_ERROR)
		return E_Fail;
	else
		return NoError;
}


UdpSendAnswer::UdpSendAnswer(SOCKET s, const sockaddr_in &addr)
{
	m_sock = s;
	m_addr = addr;
}

ISendAnswer::Recode_et
UdpSendAnswer::SendAnswer(const void *pSend, int bytes)
{
	int re = sendto(m_sock, (const char*)pSend, bytes, 0, (sockaddr*)&m_addr, sizeof(sockaddr_in));

	if(re==SOCKET_ERROR)
		return E_Fail;
	else
		return NoError;
}
