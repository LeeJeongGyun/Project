#include "pch.h"
#include "CLanClient.h"

CLanClient::CLanClient()
{
	_bStart = false;
	_bSendFlag = false;
	_bLoginPacket = false;
}

CLanClient::~CLanClient()
{
}

bool CLanClient::NetworkInit(const WCHAR* szIP)
{
	_clntSock = socket(AF_INET, SOCK_STREAM, 0);
	if (_clntSock == INVALID_SOCKET)
	{
		Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"socket Error: %d\n", WSAGetLastError());
		return false;
	}

	SOCKADDR_IN sockAdr = {};
	sockAdr.sin_family = AF_INET;

	_port = htons(0);
	sockAdr.sin_port = _port;
	InetPtonW(AF_INET, szIP, &sockAdr.sin_addr);

	wmemcpy(_szIP, szIP, dfIP_LEN);

	if (bind(_clntSock, (SOCKADDR*)&sockAdr, sizeof(sockAdr)) == SOCKET_ERROR)
	{
		Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"Bind Error: %d\n", WSAGetLastError());
		return false;
	}

	return true;
}

bool CLanClient::Connect(WCHAR* szBindIp, WCHAR* szServerIP, unsigned short port, int numOfWorkerThread, bool bNagleOn)
{
	if (_bStart == false)
	{
		_bStart = true;

		WSADATA wsaData;

		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"WSAStartUp Error: %d\n", WSAGetLastError());
			return false;
		}

		if (!NetworkInit(szBindIp))
			return false;

		_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
		if (_iocpHandle == NULL)
		{
			Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"Iocp Handle Error: %d\n", WSAGetLastError());
			return false;
		}

		HANDLE hWorkerThread;
		for (int iCnt = 0; iCnt < numOfWorkerThread; iCnt++)
		{
			hWorkerThread = (HANDLE)_beginthreadex(nullptr, 0, StaticWorkerThread, this, 0, nullptr);
			if (hWorkerThread == 0)
			{
				Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"Create WorkerThread Error");
				return false;
			}

			_threads.push_back(hWorkerThread);
		}

		HANDLE hConnectThread = (HANDLE)_beginthreadex(nullptr, 0, StaticConnectThread, this, 0, nullptr);
		if (hConnectThread == 0)
		{
			Logger::GetInstance()->Log(L"CLIENT_INIT", eLOG_ERROR, L"Create ConnectThread Error");
			return false;
		}

		_threads.push_back(hConnectThread);

		wmemcpy(_szServerIP, szServerIP, dfIP_LEN);
		_serverPort = port;
		_bNagle = bNagleOn;
		_numOfWorkerThread = numOfWorkerThread;
	}
	else
	{
		if (!NetworkInit(szBindIp))
			return false;
	}

	SOCKADDR_IN sockAdr = {};
    sockAdr.sin_family = AF_INET;
    InetPtonW(AF_INET, szServerIP, &sockAdr.sin_addr);
    sockAdr.sin_port = htons(port);

    if (connect(_clntSock, (SOCKADDR*)&sockAdr, sizeof(sockAdr)) == SOCKET_ERROR)
    {
        Logger::GetInstance()->Log(L"NetworkInit", eLOG_ERROR, L"Connect Error: %d\n", WSAGetLastError());
        return false;
    }

	if (!bNagleOn)
	{
		bool bNagle = bNagleOn;
		setsockopt(_clntSock, IPPROTO_TCP, TCP_NODELAY, (char*)&bNagle, sizeof(bNagle));
	}

	_bConnect = true;
	_pSession = new stSession;
	SetSession(_pSession, _clntSock, szServerIP, port);

    OnEnterJoinServer();

	CreateIoCompletionPort((HANDLE)_clntSock, _iocpHandle, (ULONG_PTR)_pSession, 0);

	RecvPost();

    return true;
}

bool CLanClient::Disconnect()
{
    closesocket(_clntSock);
	_clntSock = INVALID_SOCKET;
    return true;
}

bool CLanClient::SendPacket(Packet* pPacket)
{
	// TODO
	// 문제 존재한다. 멀티스레드로 동작시에... 해결해야돼!!
	if (_pSession == nullptr || _bConnect == false || _pSession->ioRefCount == 0)
	{
		return false;
	}
	
	// NOTE
	// 연결된 서버와 끊어졌을 경우를 대비
	if (!_bLoginPacket)
	{
		_bLoginPacket = true;
		Packet* pLoginPacket = Packet::Alloc();
		OnSetLoginPacket(pLoginPacket);
		_pSession->sendQ.Enqueue(pLoginPacket->GetBufferPtr(), pLoginPacket->GetUseSize());
		SendPost();
		if (pLoginPacket->SubRef() == 0)
			Packet::Free(pLoginPacket);
	}

	stPacketHeaderLan header;
	header.len = pPacket->GetUseSize() - sizeof(stPacketHeaderLan);
	pPacket->InputHeadData((char*)&header, sizeof(stPacketHeaderLan));
	_pSession->sendQ.Enqueue(pPacket->GetBufferPtr(), pPacket->GetUseSize());
	
	if (_bSendFlag)
	{
		_bSendFlag = false;
		SendPost();
	}
	
	return true;
}

void CLanClient::SetSession(stSession* pSession, SOCKET sock, WCHAR* ip, WORD port)
{
	pSession->clntSock = sock;
	pSession->port = port;
	pSession->recvQ.ClearBuffer();
	pSession->sendQ.ClearBuffer();

	wmemcpy(pSession->szIP, ip, dfIP_LEN);
	pSession->ioRefCount = 0;
	pSession->sendFlag = true;
}

void CLanClient::RecvPost()
{
	// NOTE
	// CancelIoEx 호출 시 IO관련 송수신 걸지 않겠다.

	stSession* pSession = _pSession;
	if (pSession->bCancelFlag)
		return;

	WSABUF buf[2];
	int bufferCount = 0;
	ULONG recvByte = 0;
	ULONG flag = 0;

	int directEnqueSize = pSession->recvQ.DirectEnqueueSize();
	int freeSize = pSession->recvQ.GetFreeSize();

	if (directEnqueSize < freeSize)
	{
		buf[0].buf = pSession->recvQ.GetRearBufferPtr();
		buf[0].len = directEnqueSize;
		buf[1].buf = pSession->recvQ.GetBufferPtr();
		buf[1].len = freeSize - directEnqueSize;
		bufferCount = 2;
	}
	else
	{
		buf[0].buf = pSession->recvQ.GetRearBufferPtr();
		buf[0].len = directEnqueSize;
		bufferCount = 1;
	}

	memset(&pSession->recvOverlapped, 0, sizeof(stMyOverlapped));
	pSession->recvOverlapped.type = eRECV;
	InterlockedIncrement(&pSession->ioRefCount);
	if (SOCKET_ERROR == WSARecv(pSession->clntSock, buf, bufferCount, &recvByte, &flag, reinterpret_cast<WSAOVERLAPPED*>(&pSession->recvOverlapped), NULL))
	{
		int iErrCode = WSAGetLastError();
		if (iErrCode != WSA_IO_PENDING)
		{
			HandleError(iErrCode);

			if (InterlockedDecrement(&pSession->ioRefCount) == 0)
			{
				DisconnectSession(pSession);
			}
		}
	}
}

void CLanClient::SendPost()
{
	// NOTE
	// CancelIoEx 호출 시 IO관련 송수신 걸지 않겠다.
	stSession* pSession = _pSession;
	if (pSession->bCancelFlag)
		return;

	WSABUF buf[2];
	int bufferCount = 0;
	ULONG sendByte = 0;
	ULONG flag = 0;

	while (true)
	{
		if (false == InterlockedExchange8(reinterpret_cast<char*>(&pSession->sendFlag), false))
			return;

		if (pSession->sendQ.GetUseSize() == 0)
		{
			InterlockedExchange8(reinterpret_cast<char*>(&pSession->sendFlag), true);

			if (pSession->sendQ.GetUseSize() != 0)
			{
				continue;
			}
			return;
		}
		else
			break;
	}

	int directDequeSize = pSession->sendQ.DirectDequeueSize();
	int useSize = pSession->sendQ.GetUseSize();

	if (directDequeSize > useSize)
	{
		buf[0].buf = pSession->sendQ.GetFrontBufferPtr();
		buf[0].len = directDequeSize;
		buf[1].buf = pSession->sendQ.GetBufferPtr();
		buf[1].len = useSize - directDequeSize;
		bufferCount = 2;
	}
	else
	{
		buf[0].buf = pSession->sendQ.GetFrontBufferPtr();
		buf[0].len = directDequeSize;
		bufferCount = 1;
	}

	memset(&pSession->sendOverlapped, 0, sizeof(stMyOverlapped));
	pSession->sendOverlapped.type = eSEND;
	InterlockedIncrement(&pSession->ioRefCount);
	if (SOCKET_ERROR == WSASend(pSession->clntSock, buf, bufferCount, &sendByte, 0, reinterpret_cast<LPWSAOVERLAPPED>(&pSession->sendOverlapped), nullptr))
	{
		int iErrCode = WSAGetLastError();
		if (iErrCode != WSA_IO_PENDING)
		{
			HandleError(iErrCode);

			if (InterlockedDecrement(&pSession->ioRefCount) == 0)
			{
				// 세션 정리
				DisconnectSession(pSession);
			}
		}
	}
}

void CLanClient::DisconnectSession(stSession* pSession)
{
	// TODO 
	// SendPacket 동작시에 추가해야돼 코드!!

	if(_clntSock != INVALID_SOCKET)
		closesocket(pSession->clntSock);
	
	delete pSession;
	_pSession = NULL;

	_bConnect = false;
	_bLoginPacket = false;
	OnLeaveServer();
}

void CLanClient::HandleError(int iErrCode)
{
	switch (iErrCode)
	{
	case WSAECONNRESET:
	case WSAECONNABORTED:
		break;
	case WSAENOBUFS:
		Logger::GetInstance()->Log(L"HANDLE_ERROR", eLOG_ERROR, L"# SOCKET ERROR WSAENOBUFS # Error Code : % d\n", iErrCode);
		break;
	default:
		Logger::GetInstance()->Log(L"HANDLE_ERROR", eLOG_ERROR, L"# SOCKET ERROR # Error Code : % d\n", iErrCode);
	}
}

void CLanClient::WorkerThread()
{
	while (true)
	{
		DWORD dwTransferred = 0;
		stSession* pSession = nullptr;
		stMyOverlapped* pOverlapped = nullptr;

		GetQueuedCompletionStatus(_iocpHandle, &dwTransferred, reinterpret_cast<PULONG_PTR>(&pSession), reinterpret_cast<LPOVERLAPPED*>(&pOverlapped), INFINITE);

		if (dwTransferred == 0 && pSession == nullptr && pOverlapped == nullptr)
		{
			// 워커스레드 종료
			wprintf(L"Worker Thread Exit: %d\n", GetCurrentThreadId());
			break;
		}

		if (dwTransferred != 0)
		{
			if (pOverlapped->type == eRECV)
			{
				pSession->recvQ.MoveRear(dwTransferred);

				while (true)
				{
					if (pSession->recvQ.GetUseSize() < sizeof(stPacketHeaderLan))
						break;

					stPacketHeaderLan packetHeader;
					pSession->recvQ.Peek(reinterpret_cast<char*>(&packetHeader), sizeof(stPacketHeaderLan));

					if (pSession->recvQ.GetUseSize() < sizeof(stPacketHeaderLan) + packetHeader.len)
						break;

					Packet* pPacket = Packet::Alloc();
					pSession->recvQ.MoveFront(sizeof(stPacketHeaderLan));
					pSession->recvQ.Dequeue(pPacket->GetBufferPtr(), packetHeader.len);
					pPacket->MoveWritePos(packetHeader.len);
					
					OnRecv(pPacket);

					if (pPacket->SubRef() == 0)
						Packet::Free(pPacket);
				}

				RecvPost();
			}
			else
			{
				pSession->sendQ.MoveFront(dwTransferred);
				pSession->sendFlag = true;

				OnSend(dwTransferred);

				SendPost();
			}
		}

		if (InterlockedDecrement(&pSession->ioRefCount) == 0)
		{
			DisconnectSession(pSession);
		}

		if (pSession->bCancelFlag)
			CancelIoEx(reinterpret_cast<HANDLE>(pSession->clntSock), nullptr);
	}
}

void CLanClient::ConnectThread()
{
	while (true)
	{
		Sleep(10000);
		if (!_bConnect)
		{
			// TODO
			// 소켓 재할당 해야된다.!!
			Connect(_szIP, _szServerIP, _serverPort, _numOfWorkerThread, _bNagle);
		}
	}
}
