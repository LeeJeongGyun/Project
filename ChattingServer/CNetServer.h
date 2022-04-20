#pragma once

#include "pch.h"

#pragma comment(lib, "ws2_32.lib")
using namespace std;

enum enTimeOut
{
	eBEFORE_LOGIN_TIMEOUT = 5000,
	eAFTER_LOGIN_TIMEOUT = 40000
};

class Packet;

#pragma pack(push, 1)
typedef struct stPacketHeader
{
	uint8 code;
	uint16 len;
	uint8 randKey;
	uint8 checkSum;
};
#pragma pack(pop)

enum eTYPE
{
	RECV = 0,
	SEND = 1
};

// SessionId 상위 2바이트(세션 배열의 인덱스), 하위 6바이트(세션의 Unique Id)
// 나눠서 사용함으로써 빠르게 세션배열을 검색
typedef union
{
	struct
	{
		uint64 id : 48;
		uint64 index : 16;
	}DUMMYSTRUCTNAME;

	uint64 sessionId;
}SessionId;

class CSLock
{
public:
	CSLock(CRITICAL_SECTION& lock)
		:_lock(lock)
	{
		EnterCriticalSection(&_lock);
	}
	~CSLock()
	{
		LeaveCriticalSection(&_lock);
	}
private:
	CRITICAL_SECTION& _lock;
};

class Lock
{
public:
	Lock(SRWLOCK& lock)
		:_lock(lock)
	{
		AcquireSRWLockExclusive(&_lock);
	}
	~Lock()
	{
		ReleaseSRWLockExclusive(&_lock);
	}
private:
	SRWLOCK& _lock;
};

class SharedLock
{
public:
	SharedLock(SRWLOCK& lock)
		:_lock(lock)
	{
		AcquireSRWLockShared(&_lock);
	}
	~SharedLock()
	{
		ReleaseSRWLockShared(&_lock);
	}
private:
	SRWLOCK& _lock;
};

class CNetServer
{
private:
	typedef struct stMyOverlapped
	{
		WSAOVERLAPPED overlapped;
		eTYPE type;
	};

	typedef struct stSession
	{
		stSession() :ioRefCount(0x01000000), bCancelFlag(false), maxTimeOutTick(50000), /*TEMP*/bSendDisconnectFlag(false)
		{
		}

		SOCKET					clntSock;
		WCHAR					szIP[dfIP_LEN];
		uint16					port;
		uint64					sessionId;
		
		// TimeOut 시간 설정값
		uint16					maxTimeOutTick;
		
		// Content에서 Disconnect한 것 파악 용도
		bool					bCancelFlag;

		// SendPacketAndDisconnect 파악 용도
		bool					bSendDisconnectFlag;

		// 위 변수들은 비교적 load만 일어나지만 
		// 아래 변수들은 load, store가 자주 일어나기 때문에
		// 캐시 미스율을 낮추기 위해 ioRefCount에 alignas를 사용함

		// 상위 1바이트 세션 Disconnect 확인
		// 하위 3바이트 세션의 IO 참조 카운트
		alignas(64) uint32		ioRefCount;

#ifdef dfSMART_PACKET_PTR
		LockFreeQueue<PacketPtr> sendQ;
#else
		LockFreeQueue<Packet*>	sendQ;
#endif
		RingBuffer				recvQ;

		stMyOverlapped			recvOverlapped;
		stMyOverlapped			sendOverlapped;

		// SendQ에서 꺼낸 Packet*의 개수
		uint16					sendPacketCnt;
		uint64					timeOutTick;

#ifdef dfSMART_PACKET_PTR
		PacketPtr sendPacketPtrArr[200];
#else
		Packet* sendPacketArr[200];
#endif
		// ioRefCount랑 동일 캐시라인에 존재한다면 alignas 사용해야된다.
		// Send 가능 여부 플래그
		bool		sendFlag;
	};

public:
	CNetServer();
	virtual ~CNetServer();

	//////////////////////////////////////////////////////////////////////////
	// Server 가동 함수
	//
	// Parameters: (WCHAR*) 서버 IP (uint16) 서버 Port (uint8) GQCS에서 대기할 스레드 수 (uint8) Running될 스레드 수 (int32) 서버 최대 사용자 수, (bool) timeOut 여부 (bool) Nagle 여부 (bool) IOpending 여부
	// Return: (bool) 시작 가능 여부
	//////////////////////////////////////////////////////////////////////////
	bool Start(const WCHAR* szIP, uint16 port, uint8 numOfWorkerThread, uint8 numOfRunningThread, int32 iMaxSessionCount, bool onTimeOut, bool onNagle, bool bZeroCopy);

	//////////////////////////////////////////////////////////////////////////
	// 서버 정지 함수
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void Stop();

	//////////////////////////////////////////////////////////////////////////
	// 현재 연결된 세션 수
	//
	// Parameters: 없음.
	// Return: (int32) 세션 수
	//////////////////////////////////////////////////////////////////////////
	int32 GetSessionCount() { return _sessionCount; }

	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠에서 해당 세션 끊을 때 사용하는 함수
	//
	// Parameters: (uint64) 끊을 세션 아이디
	// Return: (bool) 성공 여부
	//////////////////////////////////////////////////////////////////////////
	bool Disconnect(uint64 sessionId);


#ifdef dfSMART_PACKET_PTR
	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠에서 보내고 싶은 데이터 보낼 때 호출하는 함수
	//
	// Parameters: (uint64) 해당 세션 아이디 (PacketPtr) 직렬화버퍼 스마트 포인터
	// Return: (bool) 성공 여부
	//////////////////////////////////////////////////////////////////////////
	bool SendPacket(uint64 sessionId, PacketPtr packetPtr);
#else
	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠에서 보내고 싶은 데이터 보낼 때 호출하는 함수
	//
	// Parameters: (uint64) 해당 세션 아이디 (Packet*) 직렬화버퍼 포인터
	// Return: (bool) 성공 여부
	//////////////////////////////////////////////////////////////////////////
	bool SendPacket(uint64 sessionId, Packet* pPacket);
#endif

	// TEMP
	bool SendPacketPostQueue(uint64 sessionId, Packet* pPacket);

	// TEMP
	bool SendPacketNotPostQueue(uint64 sessionId, Packet* pPacket);

	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠에서 데이터 보내고 해당 세션 끊을 때 호출하는 함수
	//
	// Parameters: (uint64) 해당 세션 아이디 (Packet*) 직렬화버퍼 포인터
	// Return: (bool) 성공 여부
	//////////////////////////////////////////////////////////////////////////
	bool SendPacketAndDisconnect(uint64 sessionId, Packet* pPacket);

	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 연결된 Ip와 Port를 컨텐츠로 전달해
	// 연결 승락여부를 확인받는 함수
	//
	// Parameters: (WCHAR*) 연결된 IP (uint16) 연결된 포트 
	// Return: (bool) 연결 승락여부 값
	//////////////////////////////////////////////////////////////////////////
	virtual bool OnConnectionRequest(WCHAR* szIp, uint16 port) abstract;

	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 연결된 세션을 컨텐츠로 알려주는 함수
	//
	// Parameters: (uint64) 연결된 세션 아이디 
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnClientJoin(uint64 sessionId) abstract;

	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 끊긴 세션을 컨텐츠로 알려주는 함수
	//
	// Parameters: (uint64) 끊긴 세션 아이디 
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnClientLeave(uint64 sessionId) abstract;

#ifdef dfSMART_PACKET_PTR
	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 받은 데이터를 컨텐츠로 주는 함수 
	//
	// Parameters: (uint64) 데이터를 받은 세션 아이디 (PacketPtr) 직렬화 버퍼 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnRecv(uint64 sessionId, PacketPtr packetPtr) abstract;
#else
	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 받은 데이터를 컨텐츠로 주는 함수 
	//
	// Parameters: (uint64) 데이터를 받은 세션 아이디 (Packet*) 직렬화 버퍼 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnRecv(uint64 sessionId, Packet* pPacket) abstract;
#endif

	//////////////////////////////////////////////////////////////////////////
	// 정해진 시간을 초과한 세션을 컨텐츠로 알려주는 함수 
	//
	// Parameters: (uint64) 정해진 시간을 초과한 세션의 id
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnTimeOut(uint64 sessionId) abstract;

	//////////////////////////////////////////////////////////////////////////
	// 네트워크 라이브러리 내부에서 얻은 에러값을 컨텐츠로 주는 함수
	//
	// Parameters: (int32) 해당 에러 코드
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	virtual void OnError(int32 iErrCode) abstract;

	//////////////////////////////////////////////////////////////////////////
	// Server IP 반환 함수
	//
	// Parameters: 없음.
	// Return: (WCHAR*) Server IP
	//////////////////////////////////////////////////////////////////////////
	WCHAR* GetServerIP() { return _szIP; }

	//////////////////////////////////////////////////////////////////////////
	// Server Port 반환 함수
	//
	// Parameters: 없음.
	// Return: (uint16) Server Port
	//////////////////////////////////////////////////////////////////////////
	uint16 GetServerPort() { return _port; }

	//////////////////////////////////////////////////////////////////////////
	// Listen Socket 반환 함수
	//
	// Parameters: 없음.
	// Return: (uint8) 스레드 개수
	//////////////////////////////////////////////////////////////////////////
	SOCKET GetListenSocket() { return _listenSock; }

	//////////////////////////////////////////////////////////////////////////
	// Thread 개수 반환 함수
	// 워커 스레드 전부 종료하고 main Thread 종료를 위한 스레드 개수를 리턴하는 함수
	//
	// Parameters: 없음.
	// Return: (uint8) 스레드 개수
	//////////////////////////////////////////////////////////////////////////
	uint8 GetThreadNum() { return _threads.size(); }

	//////////////////////////////////////////////////////////////////////////
	// Thread Handle 전달 함수
	// 워커 스레드 전부 종료하고 main Thread 종료를 위한 함수
	//
	// Parameters: 없음.
	// Return: (HANDLE*) Thread 핸들 포인터
	//////////////////////////////////////////////////////////////////////////
	HANDLE* GetThreadHandle() { return _threads.data(); }

	//////////////////////////////////////////////////////////////////////////
	// Iocp Handle 반환 함수
	//
	// Parameters: 없음.
	// Return: (HANDLE) IOCP 핸들
	//////////////////////////////////////////////////////////////////////////
	HANDLE GetIocpHandle() { return _iocpHandle; }

	//////////////////////////////////////////////////////////////////////////
	// Accept Thread TPS 반환 함수
	//
	// Parameters: 없음.
	// Return: (ulong) Accept Thread TPS 값
	//////////////////////////////////////////////////////////////////////////
	inline ulong GetAcceptTPS() { return _lastAcceptTPS; }

	//////////////////////////////////////////////////////////////////////////
	// Send Message TPS 반환 함수
	//
	// Parameters: 없음.
	// Return: (ulong) Send Message TPS 값
	//////////////////////////////////////////////////////////////////////////
	inline ulong GetSendTPS() { return _lastSendTPS; }

	//////////////////////////////////////////////////////////////////////////
	// Recv Message TPS 반환 함수
	//
	// Parameters: 없음.
	// Return: (ulong) Recv Message TPS 값
	//////////////////////////////////////////////////////////////////////////
	inline ulong GetRecvTPS() { return _lastRecvTPS; }

	//////////////////////////////////////////////////////////////////////////
	// 초당 Send 송신량
	//
	// Parameters: 없음.
	// Return: (ulong) Recv Message TPS 값
	//////////////////////////////////////////////////////////////////////////
	inline ulong GetSendBytes() { return _lastSendBytes; }

	//////////////////////////////////////////////////////////////////////////
	// Accept Total Count 값 확인
	//
	// Parameters: 없음.
	// Return: 총 Accept 수.
	//////////////////////////////////////////////////////////////////////////
	inline uint64 GetAcceptTotalCnt() { return _acceptTotalCnt; }

	//////////////////////////////////////////////////////////////////////////
	// Content에서 Session Timeout 값 설정 함수
	//
	// Parameters: (uint64) 세션 id, (uint32) timeOut 값
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SetTimeOut(uint64 sessionId, uint32 timeOutValue);

	//////////////////////////////////////////////////////////////////////////
	// Content에서 해당 컨텐츠의 최대 패킷 사이즈 설정
	//
	// Parameters: (int32) packet의 최대 크기
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SetPacketMaxSize(int32 packetMaxSize) { _packetMaxSize = packetMaxSize; }


private:
	static uint32 WINAPI StaticTimeoutThread(LPVOID pServer)
	{
		((CNetServer*)pServer)->TimeoutThread();
		return 0;
	}
	
	static uint32 WINAPI StaticTPSThread(LPVOID pServer)
	{
		((CNetServer*)pServer)->TPSThread();
		return 0;
	}
	
	static uint32 WINAPI StaticAcceptThread(LPVOID pServer)
	{
		((CNetServer*)pServer)->AcceptThread();
		return 0;
	}

	static uint32 WINAPI StaticWorkerThread(LPVOID pServer)
	{
		((CNetServer*)pServer)->WorkerThread();
		return 0;
	}
	
	//응답이 없는 User 끊어내기 위한 Timeout 체크 스레드
	void TimeoutThread();

	// Send Message, Recv Message TPS 처리하는 스레드
	void TPSThread();

	// Accept Thread	
	void AcceptThread();

	// Worker Thread
	void WorkerThread();

	//////////////////////////////////////////////////////////////////////////
	// 소켓 API 사용하기 위한 초기화 함수
	//
	// Parameters: 없음.
	// Return: (bool) 소켓 API 초기화 성공여부
	//////////////////////////////////////////////////////////////////////////
	bool NetworkInit();

	//////////////////////////////////////////////////////////////////////////
	// WSARecv 래핑 함수
	//
	// Parameters: (stSession*) 수신 대상이 되는 세션 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void RecvPost(stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// WSASend 래핑 함수
	//
	// Parameters: (stSession*) 송신 대상이 되는 세션 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SendPost(stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// Session Release 함수
	//
	// Parameters: (stSession*) 릴리즈될 세션 포인터
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void DisconnectSession(stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// Session 초기화 함수
	//
	// Parameters: (uint16) 비어있는 세션배열의 인덱스 (SOCKET) socket (uint64) 세션 아이디 (WCHAR*) Ip (uint16) Port
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void SetSession(uint16 index, SOCKET sock, uint64 sessionId, WCHAR* ip, uint16 port);

	//////////////////////////////////////////////////////////////////////////
	// Error 처리 함수
	//
	// Parameters: (int32) Error Code
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void HandleError(int32 errCode);

	//////////////////////////////////////////////////////////////////////////
	// 세션을 안전하게 획득했는지 확인하는 함수
	//
	// Parameters: 없음.
	// Return: (bool) 세션 획득 가능 여부
	//////////////////////////////////////////////////////////////////////////
	bool AcquireSession(uint64 sessionId, stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// 세션을 릴리즈 할 수 있는지 확인하는 함수
	//
	// Parameters: 없음
	// Return: (bool) 세션 릴리즈 가능 여부
	//////////////////////////////////////////////////////////////////////////
	inline bool ReleaseSession(stSession* pSession);

	//////////////////////////////////////////////////////////////////////////
	// ioRefCount로부터 releaseFlag 얻는 함수
	//
	// Parameters: (uint32) ioRefCount
	// Return: (bool) 릴리즈 플래그 값
	//////////////////////////////////////////////////////////////////////////
	inline bool GetReleaseFlag(uint32& ioRefCount) { return ioRefCount >> 24; }

private:
	WCHAR								_szIP[32];
	uint16								_port;
	SOCKET								_listenSock;
	HANDLE								_iocpHandle;
	bool								_bOnNagle;
	bool								_bOnZeroCopy;
	int32								_packetMaxSize;

	// SendPacketThread용 이벤트
	stSession*							_sessionArr;

	// 최대 동접자 수
	uint32								_maxSessionCount;

	// 위 변수들은 load만 일어나지만 
	// 아래 변수들은 load, store가 자주 일어나기 때문에
	// 캐시 미스율을 낮추기 위해 _uniqueSesisonId에 alignas를 사용함

	alignas(64) uint64					_uniqueSessionId;
	uint32								_sessionCount;
	vector<HANDLE>						_threads;

	// 비어있는 세션배열의 인덱스 관리용 스택
	LockFreeStack<uint16>				_indexStack;

	uint64								_acceptTotalCnt = 0;
	ulong								_lastAcceptTPS = 0;
	ulong								_lastSendTPS = 0;
	ulong								_lastRecvTPS = 0;
	ulong								_lastSendBytes = 0;

	ulong								_currentAcceptTPS = 0;
	ulong								_currentSendTPS = 0;
	alignas(64) ulong					_currentRecvTPS = 0;
	alignas(64) ulong					_currentSendBytes = 0;
};

inline bool CNetServer::ReleaseSession(stSession* pSession)
{
	// NOTE
	// ioRefCount의 상위 1Byte는 현재 DisconnectSession이 실행됐는지 판단
	if (InterlockedCompareExchange(&pSession->ioRefCount, 0x01000000, 0) != 0)
		return false;

	return true;
}