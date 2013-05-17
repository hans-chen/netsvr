#ifndef SendAnswer_h_
#define SendAnswer_h_

class ISendAnswer
{
public:
	enum Recode_et { 
		NoError = 0, 
		E_Fail = -1
	};

	virtual Recode_et SendAnswer(const void *pSend, int bytes) = 0;
};


class TcpSendAnswer : public ISendAnswer
{
public:
	virtual Recode_et SendAnswer(const void *pSend, int bytes);

	TcpSendAnswer(SOCKET sock);
private:
	SOCKET m_sock;
};


class UdpSendAnswer : public ISendAnswer
{
public:
	virtual Recode_et SendAnswer(const void *pSend, int bytes);

	UdpSendAnswer(SOCKET sock, const sockaddr_in &addr);

private:
	SOCKET m_sock;
	sockaddr_in m_addr;
};


#endif
