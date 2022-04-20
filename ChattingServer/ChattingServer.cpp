#include "pch.h"
#include "ChattingServer.h"
#include "CLanMonitorClient.h"

extern CpuUsage g_CPUTime;
extern PDHMonitor g_Monitor;

ChattingServer::ChattingServer()
	:_beforeLoginTimeOut(0), _afterLoginTimeOut(0)
{
	_parser.LoadFile(L"ChattingServer.txt");
	HANDLE hUpdateThread = (HANDLE)_beginthreadex(nullptr, 0, StaticUpdateThread, this, 0, nullptr);
	_contentThreads.push_back(hUpdateThread);

	HANDLE hContentTPSThread = (HANDLE)_beginthreadex(nullptr, 0, StaticContentTPSThread, this, 0, nullptr);
	_contentThreads.push_back(hContentTPSThread);

	HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);
	HANDLE hExitEvent = CreateEvent(nullptr, true, false, nullptr);
	_hEvent[0] = hEvent;
	_hEvent[1] = hExitEvent;
}

ChattingServer::~ChattingServer()
{
}

void ChattingServer::Start()
{
	WCHAR szBindIP[32];
	int32 bindPort;
	int32 iocpWorkerThreadCnt;
	int32 iocpActiveThreadCnt;
	int32 maxUserCount;
	int32 packetCode;
	int32 packetKey;
	int32 onTimeOut;
	int32 onNagle;
	int32 onZeroCopy;
	int32 packetMaxSize;

	_parser.GetString(L"BIND_IP", szBindIP);
	_parser.GetValue(L"BIND_PORT", &bindPort);
	_parser.GetValue(L"IOCP_WORKER_THREAD", &iocpWorkerThreadCnt);
	_parser.GetValue(L"IOCP_ACTIVE_THREAD", &iocpActiveThreadCnt);
	_parser.GetValue(L"USER_MAX", &maxUserCount);
	_parser.GetValue(L"PACKET_CODE", &packetCode);
	_parser.GetValue(L"PACKET_KEY", &packetKey);
	_parser.GetValue(L"PACKET_MAX_SIZE", &packetMaxSize);
	_parser.GetValue(L"ON_TIMEOUT", &onTimeOut);
	_parser.GetValue(L"ON_NAGLE", &onNagle);
	_parser.GetValue(L"ON_ZEROCOPY", &onZeroCopy);
	_maxUserCount = maxUserCount;

	// Client와 Server 간 약속된 패킷 헤더의 코드 값 세팅
	Packet::_packetCode = packetCode;
	// Client와 Server 간 패킷 Encoding을 위한 약속된 고정 키 값 세팅
	Packet::_packetKey = packetKey;
	// 해당 서버의 packet Max Size 설정
	SetPacketMaxSize(packetMaxSize);

	if (onTimeOut)
	{
		_parser.GetValue(L"BEFORE_TIMEOUT_SEC", &_beforeLoginTimeOut);
		_parser.GetValue(L"AFTER_TIMEOUT_SEC", &_afterLoginTimeOut);
	}

	CNetServer::Start(szBindIP, bindPort, iocpWorkerThreadCnt, iocpActiveThreadCnt, 50000, onTimeOut, onNagle, onZeroCopy);

	HANDLE hSendDataMonitorThread = (HANDLE)_beginthreadex(nullptr, 0, StaticSendDataToMonitorThread, this, 0, nullptr);
	_contentThreads.push_back(hSendDataMonitorThread);
}

void ChattingServer::OnClientJoin(uint64 sessionId)
{
	JobPacket* jobPacket = _jobPacketPool.Alloc();
	SetJobPacket(jobPacket, eON_JOIN, sessionId);

	_jobQueue.Enqueue(jobPacket);
	SetEvent(_hEvent[0]);
}

bool ChattingServer::OnConnectionRequest(WCHAR* szIP, uint16 port)
{
	// TODO
	// 정해놓은 ip, port 매칭 작업
	return true;
}

void ChattingServer::OnClientLeave(uint64 sessionId)
{
	JobPacket* jobPacket = _jobPacketPool.Alloc();
	SetJobPacket(jobPacket, eON_LEAVE, sessionId);
	
	_jobQueue.Enqueue(jobPacket);
	SetEvent(_hEvent[0]);
}

void ChattingServer::OnRecv(uint64 sessionId, Packet* pPacket)
{
	JobPacket* jobPacket = _jobPacketPool.Alloc();
	SetJobPacket(jobPacket, eON_RECV, sessionId, pPacket);
	pPacket->AddRef();

	_jobQueue.Enqueue(jobPacket);
	SetEvent(_hEvent[0]);
}

void ChattingServer::OnTimeOut(uint64 sessionId)
{
	JobPacket* jobPacket = _jobPacketPool.Alloc();
	SetJobPacket(jobPacket, eON_TIMEOUT, sessionId);

	_jobQueue.Enqueue(jobPacket);
	SetEvent(_hEvent[0]);
}

void ChattingServer::OnError(int32 errorCode)
{
}

void ChattingServer::UpdateThread(void)
{
	JobPacket* pJobPacket;
	uint16 messageType;
	User* pUser;

	while (true)
	{
		ulong index = WaitForMultipleObjects(2, _hEvent, FALSE, INFINITE);
		if (index == WAIT_OBJECT_0 + 1)
		{
			// 종료
			break;
		}

		while (_jobQueue.GetUseCount())
		{
			_jobQueue.Dequeue(pJobPacket);

			if (pJobPacket->jobPacketType == eON_RECV)
			{
				USE_PROFILER(L"ON_RECV");
				_currentContentTPS++;
				Packet* pPacket;

				try
				{
					pPacket = pJobPacket->pPacket;
					*pPacket >> messageType;

					switch (messageType)
					{
					case en_PACKET_CS_CHAT_REQ_LOGIN:
						_currentContentLoginTPS++;
						netPacketProc_Login(pJobPacket->sessionId, pPacket);
						break;
					case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
						_currentContentSectorMoveTPS++;
						netPacketProc_SectorMove(pJobPacket->sessionId, pPacket);
						break;
					case en_PACKET_CS_CHAT_REQ_MESSAGE:
						_currentContentMessageTPS++;
						netPacketProc_Message(pJobPacket->sessionId, pPacket);
						break;
					default:
						Logger::GetInstance()->Log(L"CONTENT_ERROR", eLOG_ERROR, L"Message Type Error: %d, SessionId: %llu\n",
							messageType, pJobPacket->sessionId);
						
						Disconnect(pJobPacket->sessionId);
					}
				}
				catch (PacketOutputException& exception)
				{
					Logger::GetInstance()->LogHex(L"CONTENT_ERROR", eLOG_ERROR, L"Packet Output Error", 
						(BYTE*)exception._buffer, exception._bufferSize);
					
					Disconnect(pJobPacket->sessionId);
				}

				if (pPacket->SubRef() == 0)
				{
					Packet::Free(pPacket);
				}
			}
			else if (pJobPacket->jobPacketType == eON_JOIN)
			{
				USE_PROFILER(L"ON_JOIN");
				// NOTE
				// 유저 객체를 만드는 시점은 로그인 메시지가 왔을 때다.
				// Before Login Timeout 값 설정
				SetTimeOut(pJobPacket->sessionId, _beforeLoginTimeOut);
			}
			else if (pJobPacket->jobPacketType == eON_LEAVE)
			{
				USE_PROFILER(L"ON_LEAVE");
				// 로그인이 안되고 종료되는 경우 컨테이너에 없는 경우 발생할 수 있다. 
				pUser = FindUser(pJobPacket->sessionId);
				if (pUser != nullptr)
				{
					if (pUser->bPosInitFlag)
					{
						int16 sectorX = pUser->sectorX;
						int16 sectorY = pUser->sectorY;
						
						for (auto iter = _userList[sectorY][sectorX].begin(); iter != _userList[sectorY][sectorX].end(); ++iter)
						{
							if ((*iter) == pUser)
							{
								_userList[sectorY][sectorX].erase(iter);
								break;
							}
						}
					}

					if (false == EraseUser(pJobPacket->sessionId))
					{
						Logger::GetInstance()->Log(L"ChattingServer", eLOG_ERROR, L"Leave, No SessionId\n");
						CrashDump::Crash();
					}
				
					_userPool.Free(pUser);
				}
			}
			else
			{
				Logger::GetInstance()->Log(L"TimeOUT", eLOG_ERROR, L"TimeOut SessionId: %llu\n", pJobPacket->sessionId);
				Disconnect(pJobPacket->sessionId);
			}


			//////////////// TEMP ////////////////////// 
			// 20ms 마다 한번씩 Send 해준다.
			static uint32 beginTick = GetTickCount();
			if (GetTickCount() - beginTick > 20)
			{
				auto beginIter = _userHashMap.begin();
				auto endIter = _userHashMap.end();
				for (auto iter = beginIter; iter != endIter; ++iter)
				{
					SendPacketPostQueue(iter->second->sessionId, nullptr);
				}
				
				beginTick = GetTickCount();
			}

			//////////////////////////////////////

			_jobPacketPool.Free(pJobPacket);
			_currentUpdateTPS++;
		}
	}
}

void ChattingServer::ContentTPSThread(void)
{
	while (true)
	{
		_lastUpdateTPS = _currentUpdateTPS;
		_currentUpdateTPS = 0;

		_lastSectorAvgPlayerNum = _currentSectorAvgPlayerNum;

		_lastContentTPS = _currentContentTPS;
		_lastContentLoginTPS = _currentContentLoginTPS;
		_lastContentSectorMoveTPS = _currentContentSectorMoveTPS;
		_lastContentMessgaeTPS = _currentContentMessageTPS;
		
		_currentContentTPS = 0;
		_currentContentLoginTPS = 0;
		_currentContentSectorMoveTPS = 0;
		_currentContentMessageTPS = 0;

		Sleep(1000);
	}
}

void ChattingServer::SendDataToMonitorThread(void)
{
	CLanMonitorClient monitorClient;
	Packet* pPacket = Packet::Alloc();
	Sleep(2000);

	while (true)
	{
		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1, time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, g_CPUTime.GetProcessTotal(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, g_Monitor.GetProcessUserMemoryUsage() / 1024 / 1024, time(NULL));
		monitorClient.SendPacket(pPacket);
		
		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_SESSION, GetSessionCount(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_PLAYER, _userHashMap.size(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, GetUpdateTPS(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, Packet::_packetPool.GetUseCount(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, _jobQueue.GetUseCount(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL, g_CPUTime.GetProcessorTotal(), time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY, g_Monitor.GetNonpagedPoolUsage() / 1024 / 1024, time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV, g_Monitor.GetEthernetRecvBytes() / 1024, time(NULL));
		monitorClient.SendPacket(pPacket);

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND, g_Monitor.GetEthernetSendBytes() / 1024, time(NULL));
		monitorClient.SendPacket(pPacket);

		// SendPacket 내부 SendPost 활성화
		monitorClient.ActiveSendPacket();

		monitorClient.mp_MonitorData(pPacket, en_PACKET_SS_MONITOR_DATA_UPDATE, dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY, g_Monitor.GetAvailableMemoryUsage(), time(NULL));
		monitorClient.SendPacket(pPacket);

		Sleep(1000);
	}

	if (pPacket->SubRef() == 0)
		Packet::Free(pPacket);
}

void ChattingServer::SetUser(User* pUser, uint64 sessionId, int64 accNo, WCHAR* pID, WCHAR* pNickName)
{
	pUser->accountNo = accNo;
	pUser->sessionId = sessionId;
	wmemcpy(pUser->szID, pID, sizeof(pUser->szID) / sizeof(WCHAR));
	wmemcpy(pUser->szNickName, pNickName, sizeof(pUser->szNickName) / sizeof(WCHAR));
	pUser->bPosInitFlag = false;
}


void ChattingServer::netPacketProc_Login(uint64 sessionId, Packet* pPacket)
{
	User* pUser;
	int64 accountNo;
	WCHAR szIP[20];
	WCHAR szNickName[20];
	char sessionKey[64];

	// 동접 제한
	if (_userPool.GetUseCount() >= _maxUserCount)
	{
		Logger::GetInstance()->Log(L"CONTENT_ERROR", eLOG_ERROR, L"[Over MaxUserCount]\n");
		Disconnect(sessionId);
		return;
	}

	// 패킷 크기 오류
	if (pPacket->GetUseSize() != 152)
	{
		Logger::GetInstance()->Log(L"CONTENT_ERROR", eLOG_ERROR, L"Login Packet Size Error\n");
		Disconnect(sessionId);
		return;
	}

	*pPacket >> accountNo;
	pPacket->GetData((char*)szIP, sizeof(szIP));
	pPacket->GetData((char*)szNickName, sizeof(szNickName));
	pPacket->GetData((char*)sessionKey, sizeof(sessionKey));

	// SessionKey 맞는지 확인

	pUser = _userPool.Alloc();
	SetUser(pUser, sessionId, accountNo, szIP, szNickName);
	InsertUser(sessionId, pUser);

	// Login Timeout 설정
	SetTimeOut(sessionId, _afterLoginTimeOut);

	mp_Login(pPacket, en_PACKET_CS_CHAT_RES_LOGIN, 1, pUser->accountNo);
	SendPacket(sessionId, pPacket);
}

void ChattingServer::netPacketProc_SectorMove(uint64 sessionId, Packet* pPacket)
{
	User* pUser;
	int64 accountNo;
	uint16 sectorX;
	uint16 sectorY;

	// 패킷 크기 오류
	if (pPacket->GetUseSize() != 12)
	{
		Disconnect(sessionId);
		return;
	}

	*pPacket >> accountNo >> sectorX >> sectorY;

	if (sectorX >= 50 || sectorX < 0 || sectorY >= 50 || sectorY < 0)
	{
		Logger::GetInstance()->Log(L"[CONTENT_ERROR]", eLOG_ERROR, L"Secotr Rnage Error, SessionId: %llu", sessionId);
		Disconnect(sessionId);
		return;
	}

	pUser = FindUser(sessionId);
	if (pUser == nullptr)
	{
		Logger::GetInstance()->Log(L"CRASH_ERROR", eLOG_ERROR, L"SectorMove, No SessionId\n");
		CrashDump::Crash();
		return;
	}

	// AccountNo 불일치 오류
	if (pUser->accountNo != accountNo)
	{
		Disconnect(sessionId);
		return;
	}

	// Sector 이동
	if (!pUser->bPosInitFlag)
	{
		pUser->bPosInitFlag = true;
		_userList[sectorY][sectorX].push_back(pUser);
	}
	else
	{
		list<User*>& pList = _userList[pUser->sectorY][pUser->sectorX];
		auto endIter = pList.end();

		for (auto iter = pList.begin(); iter != endIter; ++iter)
		{
			if ((*iter) == pUser)
			{
				pList.erase(iter);
				break;
			}
		}

		_userList[sectorY][sectorX].push_back(pUser);
	}
	
	pUser->sectorX = sectorX;
	pUser->sectorY = sectorY;

	mp_SectorMove(pPacket, en_PACKET_CS_CHAT_RES_SECTOR_MOVE, pUser->accountNo, sectorX, sectorY);
	SendPacket(sessionId, pPacket);
}

void ChattingServer::netPacketProc_Message(uint64 sessionId, Packet* pPacket)
{
	User* pUser;
	int64 accountNo;
	uint16 messageLen;
	WCHAR message[512];

	*pPacket >> accountNo >> messageLen;

	pUser = FindUser(sessionId);
	if (pUser == nullptr)
	{
		Logger::GetInstance()->Log(L"[CRASH_ERROR]", eLOG_ERROR, L"Message, No SessionId\n");
		CrashDump::Crash();
		return;
	}

	// 패킷 크기 오류
	if (pPacket->GetUseSize() != messageLen)
	{
		Logger::GetInstance()->Log(L"[CONTENT_ERROR]", eLOG_ERROR, L"Message Len Error\n");
		Disconnect(pUser->sessionId);
		return;
	}

	// AccountNo 불일치 오류
	if (pUser->accountNo != accountNo)
	{
		Disconnect(pUser->sessionId);
		return;
	}

	pPacket->GetData((char*)message, messageLen);

	mp_Message(pPacket, en_PACKET_CS_CHAT_RES_MESSAGE, pUser->accountNo, pUser->szID, pUser->szNickName, messageLen, message);
	SendPacket_SectorAround(pUser, pPacket);
}

void ChattingServer::SendPacket_SectorAround(User* pUser, Packet* pPacket)
{
	SectorAround sectorAround;
	int32 sectorCnt;

	GetSectorAround(pUser->sectorX, pUser->sectorY, &sectorAround);
	sectorCnt = sectorAround.sectorCnt;

	for (int32 cnt = 0; cnt < sectorCnt; cnt++)
	{
		list<User*>& pList = _userList[sectorAround.sectorArr[cnt].sectorY][sectorAround.sectorArr[cnt].sectorX];
		auto endIter = pList.end();

		for (auto iter = pList.begin(); iter != endIter; iter++)
		{
			SendPacketNotPostQueue((*iter)->sessionId, pPacket);
		}
	}
}

void ChattingServer::GetSectorAround(int16 sectorX, int16 sectorY, SectorAround* pSectorAround)
{
	int16 cntX;
	int16 cntY;
	int32 sectorCnt = 0;

	--sectorX;
	--sectorY;

	// sectorY || sectorX가 < 0 인 경우 
	// (sectorY || ~~) 이런식으로 IF문 하면 계속 참이기 때문에 for문 안에 못돈다.
	for (cntY = 0; cntY < 3; cntY++)
	{
		if (sectorY + cntY < 0 || sectorY + cntY >= 50)
			continue;

		for (cntX = 0; cntX < 3; cntX++)
		{
			if (sectorX + cntX < 0 || sectorX + cntX >= 50)
				continue;

			pSectorAround->sectorArr[sectorCnt].sectorX = sectorX + cntX;
			pSectorAround->sectorArr[sectorCnt].sectorY = sectorY + cntY;
			sectorCnt++;
		}
	}

	pSectorAround->sectorCnt = sectorCnt;
}


void ChattingServer::mp_Login(Packet* pPacket, uint16 type, uint8 status, int64 accountNo)
{
	pPacket->Clear();
	pPacket->ReserveHeadSize(5);
	*pPacket << type << status << accountNo;
}

void ChattingServer::mp_SectorMove(Packet* pPacket, uint16 type, int64 accountNo, uint16 sectorX, uint16 sectorY)
{
	pPacket->Clear();
	pPacket->ReserveHeadSize(5);
	*pPacket << type << accountNo << sectorX << sectorY;
}

void ChattingServer::mp_Message(Packet* pPacket, uint16 type, int64 accountNo, WCHAR* pID, WCHAR* pNickName, uint16 messageLen, WCHAR* pMessage)
{
	pPacket->Clear();
	pPacket->ReserveHeadSize(5);
	*pPacket << type << accountNo;
	pPacket->PutData((char*)pID, 40);
	pPacket->PutData((char*)pNickName, 40);
	*pPacket << messageLen;
	pPacket->PutData((char*)pMessage, messageLen);
}