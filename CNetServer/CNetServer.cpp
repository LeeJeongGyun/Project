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

bool CNetServer::Start(const WCHAR* szIP, uint16 port, uint8 numOfWorkerThread, uint8 numOfRunningThread, int32 iMaxSessionCount, bool onTimeOut, bool onNagle, bool bZeroCopy)
{
	wmemcpy(_szIP, szIP, 32);
	_port = port;
	_bOnNagle = onNagle;
	_bOnZeroCopy = bZeroCopy;

	if (!NetworkInit())
	{
		return false;
	}

	_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, numOfRunningThread);
	if (_iocpHandle == NULL)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to create IO Completion Port, Error Code: %d\n", WSAGetLastError());
		return false;
	}

	HANDLE hTPSThread = (HANDLE)_beginthreadex(nullptr, 0, StaticTPSThread, this, 0, nullptr);
	if (hTPSThread == 0)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to create TPS Thread, Error Code: %d\n", WSAGetLastError());
		return false;
	}
	_threads.push_back(hTPSThread);

	if (onTimeOut)
	{
		HANDLE hTimeoutThread = (HANDLE)_beginthreadex(nullptr, 0, StaticTimeoutThread, this, 0, nullptr);
		if (hTimeoutThread == 0)
		{
			Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to create TimeOut Thread, Error Code: %d\n", WSAGetLastError());
			return false;
		}

		_threads.push_back(hTimeoutThread);
	}

	HANDLE hAcceptThread = (HANDLE)_beginthreadex(nullptr, 0, StaticAcceptThread, this, 0, nullptr);
	if (hAcceptThread == 0)
	{
		Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to create Accept Thread, Error Code: %d\n", WSAGetLastError());
		return false;
	}
	_threads.push_back(hAcceptThread);

	HANDLE hWorkerThread;
	for (int iCnt = 0; iCnt < numOfWorkerThread; iCnt++)
	{
		hWorkerThread = (HANDLE)_beginthreadex(nullptr, 0, StaticWorkerThread, this, 0, nullptr);
		if (hWorkerThread == 0)
		{
			Logger::GetInstance()->Log(L"SERVER_ON_ERROR", eLOG_SYSTEM, L"Fail to create Worker Thread, Error Code: %d\n", WSAGetLastError());
			return false;
		}

		_threads.push_back(hWorkerThread);
	}

	// 세션 배열 생성
	_sessionArr = new stSession[iMaxSessionCount];
	_maxSessionCount = iMaxSessionCount;

	// 인덱스 관리용 스택 초기화
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

	// AcquireSession에서 ioRefCount를 증가시켰으므로 감소
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
	stSession* pSession = nullptr;
	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = sessionId;

	pSession = &_sessionArr[uniqueSessionId.index];

	if (false == AcquireSession(sessionId, pSession))
		return false;

	pPacket->AddRef();
	pPacket->Encode();
	pSession->sendQ.Enqueue(pPacket);

	// NOTE
	// SendPost의 리턴이 오래걸리기 때문에 
	// IOCP WorkerThread가 Send 하도록 유도
	PostQueuedCompletionStatus(_iocpHandle, -1, (ULONG_PTR)pSession, nullptr);
	return true;
}
#endif

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

void CNetServer::AcceptThread()
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

		if (GetSessionCount() > _maxSessionCount)
		{
			Logger::GetInstance()->Log(L"[MAX_SESSION_COUNT]", eLOG_ERROR, L"Over Max Session Count\n");
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

		// 8byte 변수에 Unique한 세션 값(6byte) 인덱스(2byte) 를 비트로 해서 넣는다.
		// sessionId로부터 index를 바로 뽑아내서 검색하는 비용 줄이기 위한 방법
		uniqueSessionId.id = _uniqueSessionId;
		uniqueSessionId.index = index;

		SetSession(index, clntSock, uniqueSessionId.sessionId, szIp, port);

		CreateIoCompletionPort((HANDLE)(clntSock), _iocpHandle, (ULONG_PTR)(&_sessionArr[index]), 0);

		OnClientJoin(uniqueSessionId.sessionId);
		_uniqueSessionId++;

		// Session이 삭제될때 Decrement하는 것과 동시에 발생할 수 있기 때문에 Interlocked 사용
		InterlockedIncrement(&_sessionCount);

		RecvPost(&_sessionArr[index]);

		// NOTE
		// 초기화 할때 ioRefCount 증가시킨거 원상복귀하는 코드
		// 0 확인안해주면 indexStack 터진다.
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
		ulong transferred = 0;
		stSession* pSession = nullptr;
		stMyOverlapped* pOverlapped = nullptr;

		GetQueuedCompletionStatus(_iocpHandle, &transferred, reinterpret_cast<PULONG_PTR>(&pSession), reinterpret_cast<LPOVERLAPPED*>(&pOverlapped), INFINITE);

		if (transferred == 0 && pSession == nullptr && pOverlapped == nullptr)
		{
			// 워커스레드 종료
			wprintf(L"Worker Thread Exit: %d\n", GetCurrentThreadId());
			break;
		}

		if(transferred == -1)
		{
			SendPost(pSession);
		}
		else if (transferred != 0)
		{
			if (pOverlapped->type == RECV)
			{
				// Timeout용 변수 갱신
				pSession->timeOutTick = GetTickCount64();

				pSession->recvQ.MoveRear(transferred);

				while (true)
				{
					if (pSession->recvQ.GetUseSize() < sizeof(stPacketHeader))
						break;

					stPacketHeader packetHeader;
					pSession->recvQ.Peek(reinterpret_cast<char*>(&packetHeader), sizeof(stPacketHeader));

					// 이상한 Client 방지
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
				// NOTE
				// for문 돌면서 비교 대상이 되는 pSession->snedPacketCnt의 값을 가져오기위해
				// 객체 주소를 통해서 가져와야된다. 따라서 지역변수로 복사를 통해서 그런 행동 방지
				uint16 sendPacketCnt = pSession->sendPacketCnt;

				// NOTE
				// Send Message TPS
				InterlockedExchangeAdd(&_currentSendTPS, sendPacketCnt);

				// NOTE 
				// Send Bytes 송신량 계산
				InterlockedExchangeAdd(&_currentSendBytes, (transferred / 1460) * 1500 + ((transferred % 1460) + 40));
				
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

	// LINGER 옵셥
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
	// NOTE
	// CancelIoEx 호출 시 IO관련 송수신 걸지 않겠다.
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
	// NOTE
	// CancelIoEx 호출 시 IO관련 송수신 걸지 않겠다.
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

	// NOTE
	// 복사 해놓지 않고 사용한다면 sendPacketCnt에 들어가는 SendQ의 크기와
	// for문 조건에 들어가는 SendQ의 크기가 달라서 sendQ 완료통지에서 
	// Dequeue한 패킷 수만큼 Free를 시키지 못하므로 Packet leak이 발생할 수 있다.
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
	while (pSession->sendQ.Dequeue(packetPtr));

	// NOTE
	// SendQ에서 Deque는 했지만 closesocket에 의해서 완료통지는 못받은 상황이라면
	// Packet* 메모리 leak나는 상황 발생
	for (int iCnt = 0; iCnt < pSession->sendPacketCnt; iCnt++)
	{
		pSession->sendPacketPtrArr[iCnt] = 0;
	}
#else
	// NOTE
	// Enqueue는 했지만 아예 Dequeue를 못한경우 메모리 leak 발생
	Packet* pPacket;
	while (pSession->sendQ.Dequeue(pPacket))
	{
		if(pPacket->SubRef() == 0)
			Packet::Free(pPacket);
	}
	
	// NOTE
	// SendQ에서 Deque는 했지만 closesocket에 의해서 완료통지는 못받은 상황이라면
	// Packet* 메모리 leak나는 상황 발생
	for (int iCnt = 0; iCnt < pSession->sendPacketCnt; iCnt++)
	{
		if (0 == pSession->sendPacketArr[iCnt]->SubRef())
			Packet::Free(pSession->sendPacketArr[iCnt]);
	}
#endif

	OnClientLeave(pSession->sessionId);
	
	closesocket(pSession->clntSock);
	pSession->clntSock = INVALID_SOCKET;

	// CancelIoEx관련 처리
	pSession->bCancelFlag = false;

	SessionId uniqueSessionId;
	uniqueSessionId.sessionId = pSession->sessionId;

	_indexStack.Push(uniqueSessionId.index);

	InterlockedDecrement(&_sessionCount);
}

void CNetServer::SetSession(uint16 index, SOCKET sock, uint64 sessionId, WCHAR* ip, uint16 port)
{
	// NOTE
	// ioRefCount, ReleaseFlag, timeoutClock 초기화 순서 중요하다.
	_sessionArr[index].clntSock = sock;
	_sessionArr[index].sessionId = sessionId;
	_sessionArr[index].port = port;
	_sessionArr[index].bSendDisconnectFlag = false;

	_sessionArr[index].recvQ.ClearBuffer();

	wmemcpy(_sessionArr[index].szIP, ip, dfIP_LEN);

	// NOTE
	// TimeOut Thread에서 ReleaseFlag를 확인하고 로직을 돌기 때문에
	// timeOut값을 초기화 해주는 작업이 RelaseFlag를 And연산 하는 작업보다 밑에 있다면
	// 새로 들어온 세션을 끊어버리는 상황이 발생할 수 있다.
	_sessionArr[index].timeOutTick = GetTickCount64();

	// NOTE
	// 1. 연결을 하고 먼저 데이터를 보내야 하는 컨텐츠가 존재할 때 
	// 0으로 초기화를 하고 시작하면 Recv를 걸어주는 것보다 Send 완료통지
	// 로직이 먼저 돌기 때문에 DisconnectSession이 호출된다.
	// 2. Release된 세션을 대상으로 AcquireSession 로직이 실행되면 DisconnectSession
	// 로직이 2번 도는 상황 발생한다.
	InterlockedIncrement(&_sessionArr[index].ioRefCount);
	
	// NOTE
	// AcquireSession이랑 동시에 일어났을 때 만약 그냥 대입연산을 하게 된다면
	// AcquireSession에서는 1이 증가됐음에도 대입연산이 그걸 반영하지 못하는 경우 발생
	InterlockedAnd((long*)&_sessionArr[index].ioRefCount, 0x00ffffff);

	// NOTE 
	// IoRefCount 증가와 RleaseFlag And 연산의 순서가 바뀌게 되는 경우
	// And 연산이 우선시돼 ioRefCount는 0이 된 후 SendPacket이 호출되면 
	// ioRefCount++ 해서 return 값이 1이 나오기 때문에 새롭게 들어온 세션 DeleteSession하게 된다.

	_sessionArr[index].sendFlag = true;
}

void CNetServer::HandleError(int32 errCode)
{
	switch (errCode)
	{
	case WSAECONNRESET:
	case WSAECONNABORTED:
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
	// NOTE 
	// LockFree 구조에서 안전하게 세션 얻기

	// 과정 1
	// 값을 증가시켯는데 1이라는 뜻은 현재 DisconnectSession에 들어간
	// 스레드 있다고 판단하여 획득 실패
	if (1 == InterlockedIncrement(&pSession->ioRefCount))
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	// 과정 2
	// 현재 ReleaseFlag가 true라면 DisconnectSession에 들어간
	// 스레드 있다고 판단하여 획득 실패
	if (GetReleaseFlag(pSession->ioRefCount) == true)
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	// 과정 3
	// 세션 아이디가 다르다면 이미 DisconnectSession을 했다고 판단
	// 획득 실패
	if (pSession->sessionId != sessionId)
	{
		if (0 == InterlockedDecrement(&pSession->ioRefCount))
		{
			DisconnectSession(pSession);
		}

		return false;
	}

	// NOTE
	// 과정1과 과정2의 순서가 바뀌는 경우 문제가 발생할 수 있다.
	// DisconnectSession에 들어가서 ReleaseSession 하기 전에
	// 과정 2가 실행되면 거짓임으로 실행 
	// 그다음 ReleaseSession이 실행되고 1번이 실행되면 return 1이 아니기 때문에 통과
	// 아직 새로운 세션이 할당받지 않았기때문에 sessionId 동일 
	// 따라서 DisconnectSesison이 발생했음에도 통과되는 경우가 발생한다.
	// 따라서 과정1, 과정2 의 순서는 바뀌면 안된다.

	return true;
}
