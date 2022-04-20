#pragma once

#include "CNetServer.h"

struct MemoryLog
{
	uint64 cnt;
	uint64 id;
	uint64 No;
	char type;
};

enum enJOB_PACKET_TYPE
{
	eON_RECV = 0,
	eON_JOIN = 1,
	eON_LEAVE = 2,
	eON_TIMEOUT = 3
};

struct Sector
{
	int16 sectorX;
	int16 sectorY;
};

struct SectorAround
{
	int16 sectorCnt;
	Sector sectorArr[9];
};

struct JobPacket
{
	inline void PlacementNewInit() {}
	inline void Init() {}
	
	uint64 sessionId;
	uint8 jobPacketType;
	Packet* pPacket;
};

struct User
{
	inline void PlacementNewInit() {}
	inline void Init() {}

	uint64 sessionId;
	int64 accountNo;
	WCHAR szID[20];
	WCHAR szNickName[20];
	bool bPosInitFlag;

	uint16 sectorX;
	uint16 sectorY;
};

class ChattingServer : public CNetServer
{
public:
	ChattingServer();
	~ChattingServer();

	void Start();

	bool OnConnectionRequest(WCHAR* szIP, uint16 port) override;
	void OnClientJoin(uint64 sessionId) override;
	void OnClientLeave(uint64 sessionId) override;
	void OnRecv(uint64 sessionId, Packet* pPacket) override;
	void OnTimeOut(uint64 sessionId) override;
	void OnError(int32 errorCode) override;

	inline int32 GetUserPoolCount() { return _userPool.GetUseCount(); }
	inline uint32 GetJobPacketPoolSize() { return _jobPacketPool.GetUseCount(); }
	inline uint32 GetUpdateTPS() { return _lastUpdateTPS; }
	inline uint16 GetJobQueueSize() { return _jobQueue.GetUseCount(); }
	inline uint32 GetPlayerCount() const { return _userHashMap.size(); }

	static uint32 WINAPI StaticUpdateThread(LPVOID pChatServer) { ((ChattingServer*)pChatServer)->UpdateThread(); return 0; }
	static uint32 WINAPI StaticContentTPSThread(LPVOID pChatServer) { ((ChattingServer*)pChatServer)->ContentTPSThread(); return 0; }
	static uint32 WINAPI StaticSendDataToMonitorThread(LPVOID pChatServer) { ((ChattingServer*)pChatServer)->SendDataToMonitorThread(); return 0; }


private:
	void UpdateThread(void);
	void ContentTPSThread(void);
	void SendDataToMonitorThread(void);

	void SetUser(User* pUser, uint64 sessionId, int64 accNo, WCHAR* pID, WCHAR* pNickName);

	// Packet Message 로직 처리
	void netPacketProc_Login(uint64 sessionId, Packet* pPacket);
	void netPacketProc_SectorMove(uint64 sessionId, Packet* pPacket);
	void netPacketProc_Message(uint64 sessionId, Packet* pPacket);

	// Packet Setting 함수
	void mp_Login(Packet* pPacket, uint16 type, uint8 status, int64 accountNo);
	void mp_SectorMove(Packet* pPacket, uint16 type, int64 accountNo, uint16 sectorX, uint16 sectorY);
	void mp_Message(Packet* pPacket, uint16 type, int64 accountNo, WCHAR* pID, WCHAR* pNickName, uint16 messageLen, WCHAR* pMessage);

	// 컨테이너 바뀔 가능성 대비
	inline void InsertUser(uint64 sessionId, User* pUser);
	inline User* FindUser(uint64 sessionId);
	inline bool EraseUser(uint64 sessionId);

	//////////////////////////////////////////////////////////////////////////
	// UpdateThread로 넘겨줄 JobPacket Setting 함수
	//
	// Parameters: (JobPacket*) JobPacket 포인터, (enJOB_PACKET_TYPE) jobPacket의 타입, (uint64) sessionId, (Packet*) Message 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	inline void SetJobPacket(JobPacket* pJobPacket, enJOB_PACKET_TYPE jobType, uint64 sessionId, Packet* pPacket = nullptr);

	// Sector 처리 관련 함수
	void SendPacket_SectorAround(User* pUser, Packet* pPacket);
	void GetSectorAround(int16 sectorX, int16 sectorY, SectorAround* pSectorAround);

private:
	// File로 부터 세팅 값 가져오는 Parser
	Parser							_parser;

	// TimeOut 시간 변수
	int32							_beforeLoginTimeOut;
	int32							_afterLoginTimeOut;

	HANDLE							_hEvent[2];
	int32							_maxUserCount;
	LockFreeQueue<JobPacket*>		_jobQueue;
	unordered_map<uint64, User*>	_userHashMap;
	
	list<User*>						_userList[50][50];

	vector<HANDLE>					_contentThreads;
	
	MemoryPoolTLS<JobPacket>		_jobPacketPool;
	MemoryPool<User>				_userPool;

	uint32							_lastUpdateTPS = 0;
	uint32							_currentUpdateTPS = 0;

public:
	// Message TPS 비율 구하기 위한 변수
	uint32							_lastContentTPS = 0;
	uint32							_currentContentTPS = 0;

	uint32							_lastContentLoginTPS = 0;
	uint32							_currentContentLoginTPS = 0;

	uint32							_lastContentSectorMoveTPS = 0;
	uint32							_currentContentSectorMoveTPS = 0;

	uint32							_lastContentMessgaeTPS = 0;
	uint32							_currentContentMessageTPS = 0;

	uint16							_lastSectorAvgPlayerNum = 0;
	uint16							_currentSectorAvgPlayerNum = 0;
};

void ChattingServer::InsertUser(uint64 sessionId, User* pUser)
{
	_userHashMap.insert(make_pair(sessionId, pUser));
}

User* ChattingServer::FindUser(uint64 sessionId)
{
	auto findIter = _userHashMap.find(sessionId);
	if (findIter == _userHashMap.end())
		return nullptr;
	else
		return findIter->second;
}

bool ChattingServer::EraseUser(uint64 sessionId)
{
	ulong64 retValue = _userHashMap.erase(sessionId);
	if (retValue == 0)
		return false;
	else
		return true;
}

inline void ChattingServer::SetJobPacket(JobPacket* pJobPacket, enJOB_PACKET_TYPE jobType, uint64 sessionId, Packet* pPacket)
{
	pJobPacket->jobPacketType = jobType;
	pJobPacket->sessionId = sessionId;
	pJobPacket->pPacket = pPacket;
}
