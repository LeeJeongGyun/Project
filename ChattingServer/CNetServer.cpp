#include "pch.h"
#include "CNetServer.h"

CNetServer::CNetServer()
	:_uniqueSessionId(1), _sessionCount(0), _maxSessionCount(0), _sessionArr(nullptr)
{
}

CNetServer::~CNetServer()
{
	CloseHandle(_iocpHandle);

	for (auto handle : _threads)
		CloseHandle(handle);

	if (_sessionArr != nullptr)
		delete[] _sessionArr;

	_sessionArr = nullptr;
}

bool CNetServer::Start(const WCHAR* szIP, uint16 port, uint8 numOfWorkerThread, uint8 numOfRunningThread, int32 iMaxSessionCount, bool onTimeOut, bool onNagle, bool onZeroCopy)
{
	wmemcpy(_szIP, szIP, 32);
	_port = port;
	_bOnNagle = onNagle;
	_bOnZeroCopy = onZeroCopy;

	if (!NetworkInit())
	{
		return false;
	}

	_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, numOfRunningThread);
	if (_iocpHandle == NULL)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_ERROR, L"Fail to create IO Completion Port, Error Code: %d\n", WSAGetLastError());
		return false;
	}

	HANDLE hTPSThread = (HANDLE)_beginthreadex(nullptr, 0, StaticTPSThread, this, 0, nullptr);
	if (hTPSThread == 0)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_ERROR, L"Fail to create TPS Thread, Error Code: %d\n", WSAGetLastError());
		return false;
	}
	_threads.push_back(hTPSThread);

	if (onTimeOut)
	{
		HANDLE hTimeoutThread = (HANDLE)_beginthreadex(nullptr, 0, StaticTimeoutThread, this, 0, nullptr);
		if (hTimeoutThread == 0)
		{
			Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_ERROR, L"Fail to create TimeOut Thread, Error Code: %d\n", WSAGetLastError());
			return false;
		}

		_threads.push_back(hTimeoutThread);
	}

	HANDLE hAcceptThread = (HANDLE)_beginthreadex(nullptr, 0, StaticAcceptThread, this, 0, nullptr);
	if (hAcceptThread == 0)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_ERROR, L"Fail to create Accept Thread, Error Code: %d\n", WSAGetLastError());
		return false;
	}
	_threads.push_back(hAcceptThread);

	HANDLE hWorkerThread;
	for (int iCnt = 0; iCnt < numOfWorkerThread; iCnt++)
	{
		hWorkerThread = (HANDLE)_beginthreadex(nullptr, 0, StaticWorkerThread, this, 0, nullptr);
		if (hWorkerThread == 0)
		{
			Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_ERROR, L"Fail to create Worker Thread, Error Code: %d\n", WSAGetLastError());
			return false;
		}

		_threads.push_back(hWorkerThread);
	}

	_sessionArr = new stSession[iMaxSessionCount];
	_maxSessionCount = iMaxSessionCount;

	for (int32 iCnt = iMaxSessionCount - 1; iCnt >= 0; iCnt--)
		_indexStack.Push(iCnt);

	Logger::GetInstance()->Log(L"SERVER_ON", eLOG_SYSTEM, L"SERVER ON [IP: %s] [Port: %d] OK!!\n", _szIP, _port);
	return true;
}

void CNetServer::Stop()
{
	// Accept 막기
	closesocket(_listenSock);

	for (int iCnt = 0; iCnt < _maxSessionCount; iCnt++)
	{
		_sessionArr[iCnt].bCancelFlag = true;
		CancelIoEx((HANDLE)_sessionArr[iCnt].clntSock, nullptr);
	}

	while (GetSessionCount() != 0) { Sleep(500); }
}

bool CNetServer::Disconnect(uint64 sessionId)
{
	// 동일 세션을 대상으로 Disconnect할 가능성 존재
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	stSession* pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	if (!pSession->bCancelFlag)
	{
		pSession->bCancelFlag = true;
		CancelIoEx((HANDLE)pSession->clntSock, nullptr);
	}

	/*if (InterlockedExchange8(reinterpret_cast<char*>(&pSession->bCancelFlag), true) == false)
		CancelIoEx((HANDLE)pSession->clntSock, nullptr);*/

	if (0 == InterlockedDecrement(&pSession->ioRefCount))
	{
		DisconnectSession(pSession);
	}

	return true;
}

#ifdef dfSMART_PACKET_PTR
bool CNetServer::SendPacket(uint64 sessionId, PacketPtr packetPtr)
{
	stSession* pSession = nullptr;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	packetPtr._ptr->Encode();
	pSession->sendQ.Enqueue(packetPtr);

	// NOTE
	// SendPost의 리턴이 오래걸리기 때문에 
	// IOCP WorkerThread가 Send 하도록 유도
	PostQueuedCompletionStatus(_iocpHandle, -1, (ULONG_PTR)pSession, nullptr);

	return true;
}
#else
bool CNetServer::SendPacket(uint64 sessionId, Packet* pPacket)
{
	stSession* pSession;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	pPacket->AddRef();
	pPacket->Encode();
	pSession->sendQ.Enqueue(pPacket);

	PostQueuedCompletionStatus(_iocpHandle, -1, (ULONG_PTR)pSession, nullptr);
	return true;
}
#endif

// TEMP
bool CNetServer::SendPacketPostQueue(uint64 sessionId, Packet* pPacket)
{
	stSession* pSession;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	PostQueuedCompletionStatus(_iocpHandle, -1, (ULONG_PTR)pSession, nullptr);

	return true;
}

// TEMP
bool CNetServer::SendPacketNotPostQueue(uint64 sessionId, Packet* pPacket)
{
	stSession* pSession;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	pPacket->AddRef();
	pPacket->Encode();
	pSession->sendQ.Enqueue(pPacket);

	if (InterlockedDecrement(&pSession->ioRefCount) == 0)
	{
		DisconnectSession(pSession);
	}
}


bool CNetServer::SendPacketAndDisconnect(uint64 sessionId, Packet* pPacket)
{
	stSession* pSession;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	pSession->bSendDisconnectFlag = true;
	pPacket->AddRef();
	pPacket->Encode();
	pSession->sendQ.Enqueue(pPacket);

	PostQueuedCompletionStatus(_iocpHandle, -1, (ULONG_PTR)pSession, nullptr);
	return true;
}

void CNetServer::SetTimeOut(uint64 sessionId, uint32 timeOutValue)
{
	stSession* pSession;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;
	
	pSession = &_sessionArr[uniqueSessionId.index];

	if (AcquireSession(sessionId, pSession) == false)
		return;

	pSession->maxTimeOutTick = timeOutValue;

	if (InterlockedDecrement(&pSession->ioRefCount) == 0)
	{
		DisconnectSession(pSession);
	}
}

void CNetServer::TimeoutThread()
{
	while (true)
	{
		for (int32 iCnt = 0; iCnt < _maxSessionCount; iCnt++)
		{
			// NOTE
			// AcquireSesison으로 ioRefCount를 증가시키지 않기 때문에 OnTimeout호출하기 전에
			// 해당 세션이 Reelease 될 수 있음을 생각해야되기 때문에 sessionId 복사.
			uint64 sessionId = _sessionArr[iCnt].sessionId;
			if (GetReleaseFlag(_sessionArr[iCnt].ioRefCount))
				continue;

			if (GetTickCount64() - _sessionArr[iCnt].timeOutTick >= _sessionArr[iCnt].maxTimeOutTick)
				OnTimeOut(sessionId);
		}

		Sleep(3000);
	}
}

void CNetServer::TPSThread()
{
	while (true)
	{
		_lastAcceptTPS = _currentAcceptTPS;
		_lastRecvTPS = _currentRecvTPS;
		_lastSendTPS = _currentSendTPS;
		_lastSendBytes = _currentSendBytes;
		_currentAcceptTPS = 0;
		_currentRecvTPS = 0;
		_currentSendTPS = 0;
		_currentSendBytes = 0;
		Sleep(1000);
	}
	
	wprintf(L"TPS Thread Exit!!\n");
}

void CNetServer::AcceptThread(void)
{
	SOCKET clntSock;
	SOCKADDR_IN clntAdr;
	int32 iClntLen = sizeof(clntAdr);

	WCHAR szIp[dfIP_LEN];
	uint16 port;
	SessionId uniqueSessionId;
	uint16 index;

	while (true)
	{
		clntSock = accept(_listenSock, reinterpret_cast<SOCKADDR*>(&clntAdr), &iClntLen);

		// Total Accept 횟수
		_acceptTotalCnt++;

		// UpdateThreadTPS 측정용
		_currentAcceptTPS++;

		if (clntSock == INVALID_SOCKET)
		{
			wprintf(L"Accept Thread Exit\n");
			break;
		}

		if (_sessionCount >= _maxSessionCount)
		{
			Logger::GetInstance()->Log(L"MAX_SESSION_COUNT", eLOG_ERROR, L"Over Max Session Count\n");
			closesocket(clntSock);
			continue;
		}

		InetNtopW(AF_INET, &clntAdr.sin_addr, szIp, dfIP_LEN);
		port = ntohs(clntAdr.sin_port);
		if (!OnConnectionRequest(szIp, port))
		{
			Logger::GetInstance()->Log(L"[REQUEST_FAIL]", eLOG_ERROR, L"Connection Request Fail\n");
			continue;
		}
			
		_indexStack.Pop(index);

		// 8byte 변수에 Unique한 세션 값(6byte) 인덱스(2byte)를 넣는다.
		// sessionId로부터 index를 바로 뽑아내서 검색하는 비용 줄이기 위한 방법
		uniqueSessionId.id = _uniqueSessionId;
		uniqueSessionId.index = index;

		SetSession(index, clntSock, uniqueSessionId.sessionId, szIp, port);

		CreateIoCompletionPort((HANDLE)(clntSock), _iocpHandle, (ULONG_PTR)(&_sessionArr[index]), 0);

		OnClientJoin(uniqueSessionId.sessionId);
		_uniqueSessionId++;

		InterlockedIncrement(&_sessionCount);

		RecvPost(&_sessionArr[index]);

		if (0 == InterlockedDecrement(&_sessionArr[index].ioRefCount))
		{
			DisconnectSession(&_sessionArr[index]);
		}
	}
}

void CNetServer::WorkerThread()
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

		if (dwTransferred == -1)
		{
			SendPost(pSession);
		}
		else if (dwTransferred != 0)
		{
			if (pOverlapped->type == RECV)
			{
				// Timeout용 변수 갱신
				pSession->timeOutTick = GetTickCount64();

				pSession->recvQ.MoveRear(dwTransferred);

				while (true)
				{
					if (pSession->recvQ.GetUseSize() < sizeof(stPacketHeader))
						break;

					stPacketHeader packetHeader;
					pSession->recvQ.Peek(reinterpret_cast<char*>(&packetHeader), sizeof(stPacketHeader));

					// 데이터 크기 에러
					if (packetHeader.len > _packetMaxSize)
					{
						Logger::GetInstance()->Log(L"DECODE_ERROR", eLOG_ERROR, L"Packet Data Size Error: %d\n", packetHeader.len);
						Disconnect(pSession->sessionId);
						break;
					}

					if (pSession->recvQ.GetUseSize() < sizeof(stPacketHeader) + packetHeader.len)
						break;
#ifdef dfSMART_PACKET_PTR
					PacketPtr packetPtr = Packet::Alloc();
					pSession->recvQ.Dequeue(packetPtr._ptr->GetBufferPtr(), sizeof(stPacketHeader) + packetHeader.len);
					packetPtr._ptr->MoveWritePos(packetHeader.len);

					if (!packetPtr._ptr->Decode())
					{
						Logger::GetInstance()->Log(L"[CHECKSUM_ERROR]", eLOG_ERROR, L"CheckSum Error [SessionId: %llu]\n", pSession->sessionId);
						Disconnect(pSession->sessionId);
						break;
					}

					// NOTE
					// Recv Message TPS Counting
					InterlockedIncrement(&_currentRecvTPS);
					OnRecv(pSession->sessionId, packetPtr);
#else
					Packet* pPacket = Packet::Alloc();
					pSession->recvQ.Dequeue(pPacket->GetBufferPtr(), sizeof(stPacketHeader) + packetHeader.len);
					pPacket->MoveWritePos(packetHeader.len + sizeof(stPacketHeader));

					if (!pPacket->Decode())
					{
						Packet::Free(pPacket);
						Disconnect(pSession->sessionId);
						break;
					}

					// NOTE
					// Recv Message TPS Counting
					InterlockedIncrement(&_currentRecvTPS);
					OnRecv(pSession->sessionId, pPacket);

					if (pPacket->SubRef() == 0)
					{
						Packet::Free(pPacket);
					}					
#endif
				}
				RecvPost(pSession);
			}
			else
			{
				uint16 sendPacketCnt = pSession->sendPacketCnt;

				// NOTE
				// Send Message TPS
				InterlockedExchangeAdd(&_currentSendTPS, sendPacketCnt);

				// NOTE 
				// Send Bytes 송신량 계산
				InterlockedExchangeAdd(&_currentSendBytes, (dwTransferred / 1460) * 1500 + ((dwTransferred % 1460) + 40));

#ifdef dfSMART_PACKET_PTR
				for (int iCnt = 0; iCnt < sendPacketCnt; iCnt++)
				{
					pSession->sendPacketPtrArr[iCnt] = 0;
				}
#else

				for (int iCnt = 0; iCnt < sendPacketCnt; iCnt++)
				{
					if (0 == pSession->sendPacketArr[iCnt]->SubRef())
						Packet::Free(pSession->sendPacketArr[iCnt]);
				}
#endif
				pSession->sendPacketCnt = 0;
				pSession->sendFlag = true;

				SendPost(pSession);

				if (pSession->bSendDisconnectFlag)
					pSession->bCancelFlag = true;
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

bool CNetServer::NetworkInit()
{
	int32 errCode;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return false;

	_listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenSock == INVALID_SOCKET)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to make Listen Socket, Error Code: %d\n", WSAGetLastError());		
		return false;
	}

	// Linger 옵션
	LINGER lin;
	lin.l_onoff = 1;
	lin.l_linger = 0;
	setsockopt(_listenSock, SOL_SOCKET, SO_LINGER, (char*)&lin, sizeof(lin));

	// Nagle 옵션
	if (_bOnNagle)
	{
		bool bOffNagle = true;
		setsockopt(_listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&bOffNagle, sizeof(bOffNagle));

	}

	// IOPending 옵션
	if (_bOnZeroCopy)
	{ 
		int32 sendBufSize = 0;
		setsockopt(_listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufSize, sizeof(int32));
	}
	
	SOCKADDR_IN sockAdr = {};
	sockAdr.sin_family = AF_INET;
	InetPtonW(AF_INET, _szIP, &sockAdr.sin_addr);
	sockAdr.sin_port = htons(_port);

	if (SOCKET_ERROR == bind(_listenSock, reinterpret_cast<SOCKADDR*>(&sockAdr), sizeof(sockAdr)))
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to Call Bind Function, Error Code: %d\n", WSAGetLastError());
		return false;
	}

	if (listen(_listenSock, SOMAXCONN) == SOCKET_ERROR)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to Call Listen Function, Error Code: %d\n", WSAGetLastError());
		return false;
	}

	return true;
}

void CNetServer::RecvPost(stSession* pSession)
{
	if (pSession->bCancelFlag)
		return;

	WSABUF buf[2];
	int32 bufferCount = 0;
	DWORD dwRecvByte = 0;
	DWORD dwFlag = 0;

	int32 directEnqueSize = pSession->recvQ.DirectEnqueueSize();
	int32 freeSize = pSession->recvQ.GetFreeSize();

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
	pSession->recvOverlapped.type = RECV;
	InterlockedIncrement(&pSession->ioRefCount);
	if (SOCKET_ERROR == WSARecv(pSession->clntSock, buf, bufferCount, &dwRecvByte, &dwFlag, reinterpret_cast<WSAOVERLAPPED*>(&pSession->recvOverlapped), NULL))
	{
		int32 iErrCode = WSAGetLastError();
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

void CNetServer::SendPost(stSession* pSession)
{
	if (pSession->bCancelFlag)
		return;

	WSABUF buf[200];
#ifdef dfSMART_PACKET_PTR
	PacketPtr packetPtrArr[200];
#else
	Packet* packetArr[200];
#endif
	int32 bufferCount = 0;
	DWORD dwSendByte = 0;
	int32 directDequeSize;
	int32 useCount;

	while (true)
	{
		if (false == InterlockedExchange8(reinterpret_cast<int8*>(&pSession->sendFlag), false))
			return;

		if (pSession->sendQ.GetUseCount() == 0)
		{
			InterlockedExchange8(reinterpret_cast<int8*>(&pSession->sendFlag), true);

			if (pSession->sendQ.GetUseCount() != 0)
			{
				continue;
			}
			
			return;
		}
		else
			break;
	}

	useCount = pSession->sendQ.GetUseCount();
	if (useCount > 200)
		useCount = 200;
	
	pSession->sendPacketCnt = useCount;

#ifdef dfSMART_PACKET_PTR
	for (int iCnt = 0; iCnt < useCount; iCnt++)
	{
		pSession->sendQ.Dequeue(packetPtrArr[iCnt]);
		pSession->sendPacketPtrArr[iCnt] = packetPtrArr[iCnt];
		buf[iCnt].buf = packetPtrArr[iCnt]._ptr->GetBufferPtr();
		buf[iCnt].len = packetPtrArr[iCnt]._ptr->GetUseSize();
	}
#else
	for (int iCnt = 0; iCnt < useCount; iCnt++)
	{
		pSession->sendQ.Dequeue(packetArr[iCnt]);
		pSession->sendPacketArr[iCnt] = packetArr[iCnt];
		buf[iCnt].buf = packetArr[iCnt]->GetBufferPtr();
		buf[iCnt].len = packetArr[iCnt]->GetUseSize();
	}
#endif

	memset(&pSession->sendOverlapped, 0, sizeof(stMyOverlapped));
	pSession->sendOverlapped.type = SEND;
	InterlockedIncrement(&pSession->ioRefCount);

	if (SOCKET_ERROR == WSASend(pSession->clntSock, buf, pSession->sendPacketCnt, &dwSendByte, 0, reinterpret_cast<LPWSAOVERLAPPED>(&pSession->sendOverlapped), nullptr))
	{
		int32 iErrCode = WSAGetLastError();
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

void CNetServer::DisconnectSession(stSession* pSession)
{
	if (false == ReleaseSession(pSession))
		return;

#ifdef dfSMART_PACKET_PTR
	// NOTE
	// Enqueue는 했지만 아예 Dequeue를 못한경우 메모리 leak 발생
	PacketPtr packetPtr;
	while (pSession->sendQ.Dequeue(packetPtr))
	{
		packetPtr = 0;
	}

	// NOTE
	// SendQ에서 Deque는 했지만 closesocket에 의해서 완료통지는 못받은 상황이라면
	// Packet* 메모리 leak나는 상황 발생
	for (int iCnt = 0; iCnt < pSession->sendPacketCnt; iCnt++)
	{
		pSession->sendPacketPtrArr[iCnt] = 0;
	}
#else
	Packet* pPacket;
	while (pSession->sendQ.Dequeue(pPacket))
	{
		if (pPacket->SubRef() == 0)
		{
			Packet::Free(pPacket);
		}
	}

	for (int iCnt = 0; iCnt < pSession->sendPacketCnt; iCnt++)
	{
		if (0 == pSession->sendPacketArr[iCnt]->SubRef())
			Packet::Free(pSession->sendPacketArr[iCnt]);
	}
#endif

	OnClientLeave(pSession->sessionId);

	closesocket(pSession->clntSock);
	pSession->clntSock = INVALID_SOCKET;

	pSession->bCancelFlag = false;

	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = pSession->sessionId;

	_indexStack.Push(uniqueSessionId.index);

	InterlockedDecrement(&_sessionCount);
}

void CNetServer::SetSession(uint16 index, SOCKET sock, uint64 sessionId, WCHAR* ip, uint16 port)
{
	_sessionArr[index].clntSock = sock;
	_sessionArr[index].sessionId = sessionId;
	_sessionArr[index].port = port;
	_sessionArr[index].sendPacketCnt = 0;
	_sessionArr[index].bSendDisconnectFlag = false;

	_sessionArr[index].recvQ.ClearBuffer();

	wmemcpy(_sessionArr[index].szIP, ip, dfIP_LEN);

	_sessionArr[index].timeOutTick = GetTickCount64();
	
	InterlockedIncrement(&_sessionArr[index].ioRefCount);
	InterlockedAnd((long*)&_sessionArr[index].ioRefCount, 0x00ffffff);
	
	_sessionArr[index].sendFlag = true;
}

void CNetServer::HandleError(int32 errCode)
{
	switch (errCode)
	{
	case WSAECONNRESET:
	case WSAECONNABORTED:
		//case WSAEINTR:
		break;
	case WSAENOBUFS:
		Logger::GetInstance()->Log(L"[SOCKET_ERROR]", eLOG_ERROR, L"Error Code: %d\n", errCode);
		break;
	default:
		Logger::GetInstance()->Log(L"[SOCKET_ERROR]", eLOG_ERROR, L"Error Code: %d\n", errCode);
	}
}

bool CNetServer::AcquireSession(uint64 sessionId, stSession* pSession)
{
	if (1 == InterlockedIncrement(&pSession->ioRefCount))
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	if (GetReleaseFlag(pSession->ioRefCount) == true)
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	if (pSession->sessionId != sessionId)
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	return true;
}
