#pragma once

#include "pch.h"
#include<Windows.h>

#define dfIP_LEN	32

typedef struct stPacketHeaderLan
{
	short len;
};

enum enTYPE
{
	eRECV = 0,
	eSEND = 1
};

class CLanClient
{
private:

	typedef struct stMyOverlapped
	{
		WSAOVERLAPPED overlapped;
		enTYPE type;
	};

	typedef struct stSession
	{
		stSession() :ioRefCount(0), bCancelFlag(false)
		{
		}

		SOCKET					clntSock;
		WCHAR					szIP[dfIP_LEN];
		WORD					port;

		// Content에서 Disconnect한 것 파악 용도
		bool					bCancelFlag;

		// 위 변수들은 비교적 load만 일어나지만 
		// 아래 변수들은 load, store가 자주 일어나기 때문에
		// 캐시 미스율을 낮추기 위해 ioRefCount에 alignas를 사용함

		// 상위 1바이트 세션 Disconnect 확인
		// 하위 3바이트 세션의 IO 참조 카운트
		unsigned int			ioRefCount;

		RingBuffer				sendQ;
		RingBuffer				recvQ;

		stMyOverlapped			recvOverlapped;
		stMyOverlapped			sendOverlapped;

		// ioRefCount랑 동일 캐시라인에 존재한다면 alignas 사용해야된다.
		// Send 가능 여부 플래그
		alignas(64) bool		sendFlag;
	};

public:
	CLanClient();
	virtual ~CLanClient();

	bool Connect(WCHAR* szBindIP, WCHAR* szServerIP, unsigned short port, int numOfWorkerThread, bool bNagleOn);
	bool Disconnect();
	bool SendPacket(Packet* pPacket);

	virtual void OnEnterJoinServer() abstract;
	virtual void OnLeaveServer() abstract;

	virtual void OnRecv(Packet* pPacket) abstract;
	virtual void OnSend(int iSendSize) abstract;

	virtual void OnError(int iErrCode, WCHAR* szStr) abstract;
	virtual void OnSetLoginPacket(Packet* pPacket) abstract;

public:
	static unsigned int WINAPI StaticWorkerThread(LPVOID pServer)
	{
		((CLanClient*)pServer)->WorkerThread();
		return 0;
	}

	static unsigned int WINAPI StaticConnectThread(LPVOID lpParam)
	{
		((CLanClient*)lpParam)->ConnectThread();
		return 0;
	}

	void WorkerThread();

	void ConnectThread();

	bool NetworkInit(const WCHAR* szIP);

	//////////////////////////////////////////////////////////////////////////
	// Session 초기화 함수
	//
	// Parameters: (stSession*) 세션 포인터 (SOCKET) socket (WCHAR*) Ip (uint16) Port
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SetSession(stSession* pSession, SOCKET sock, WCHAR* ip, WORD port);

	//////////////////////////////////////////////////////////////////////////
	// WSARecv 래핑 함수
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void RecvPost();

	//////////////////////////////////////////////////////////////////////////
	// WSASend 래핑 함수
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SendPost();

	//////////////////////////////////////////////////////////////////////////
	// Session Release 함수
	//
	// Parameters: (stSession*) 릴리즈될 세션 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void DisconnectSession(stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// Error 처리 함수
	//
	// Parameters: (int32) Error Code
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void HandleError(int errCode);

	//////////////////////////////////////////////////////////////////////////
	// SendPacket 내부 SendPost Flag 함수
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	inline void ActiveSendPacket() { _bSendFlag = true; }

private:
	SOCKET				_clntSock;
	HANDLE				_iocpHandle;
	
	WCHAR				_szIP[dfIP_LEN];
	unsigned short		_port;
	WCHAR				_szServerIP[dfIP_LEN];
	unsigned short		_serverPort;
	int32				_numOfWorkerThread;

	bool				_bNagle;
	bool				_bStart;
	bool				_bConnect;
	bool				_bLoginPacket;

	bool				_bSendFlag;

	std::vector<HANDLE>	_threads;
	stSession*			_pSession;
};

