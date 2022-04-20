#pragma once
#include<Windows.h>
#include "MemoryPoolTLS.h"

class Packet;

class PacketOutputException : public std::exception
{
public:
	PacketOutputException(int iSize, char* buffer, int iBufferSize, int iLine)
		:_iSize(iSize), _iLine(iLine), _bufferSize(iBufferSize)
	{
		_buffer = new char[iBufferSize + 1];
		memcpy(_buffer, buffer, iBufferSize);
		_buffer[iBufferSize] = '\0';
	}

	~PacketOutputException()
	{
		delete _buffer;
	}
public:
	int _iSize;
	int _iLine;
	int _bufferSize;
	char* _buffer;
};


class Packet
{
	friend class MemoryPoolTLS<Packet>;
	friend class Chunk<Packet>;
	friend class MemoryPool<Packet>;
	enum ePACKET
	{
		eBUFFER_DEFAULT = 0x1000
	};

	enum en_LOG
	{
		enUCHAR = 1,
		enCHAR = 2,
		enUSHORT = 3,
		enSHORT = 4,
		enUINT = 5,
		enINT = 6,
		enINT64 = 7,
		enUINT64 = 8,
		enLONG = 9,
		enULONG = 10,
		enFLOAT = 11,
		enDOUBLE = 12,
		enDEFAULT = 30
	};

private:
	Packet();
	Packet(int iBufferSize);

public:
	~Packet();

	inline void Init() {}
	inline void PlacementNewInit() {}

	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠 데이터 암호화
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void Encode();

	//////////////////////////////////////////////////////////////////////////
	// 컨텐츠 데이터 복호화
	//
	// Parameters: 없음.
	// Return: (bool) 복호화 실패
	//////////////////////////////////////////////////////////////////////////
	bool Decode();

	//////////////////////////////////////////////////////////////////////////
	// 패킷  파괴.
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void	Release(void);

	//////////////////////////////////////////////////////////////////////////
	// 패킷 청소.
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	inline void	Clear(void);

	//////////////////////////////////////////////////////////////////////////
	// 버퍼 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)패킷 버퍼 사이즈 얻기.
	//////////////////////////////////////////////////////////////////////////
	int		GetBufferSize(void) { return _iBufferSize; }

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)사용중인 데이타 사이즈.
	//////////////////////////////////////////////////////////////////////////
	inline int	GetUseSize(void) { return _iUseSize; }

	//////////////////////////////////////////////////////////////////////////
	// 버퍼 포인터 얻기.
	//
	// Parameters: 없음.
	// Return: (char *)버퍼 포인터.
	//////////////////////////////////////////////////////////////////////////
	inline char* GetBufferPtr(void) { return _buffer; }


	//////////////////////////////////////////////////////////////////////////
	// 버퍼 Pos 이동. (음수이동은 안됨)
	// GetBufferPtr 함수를 이용하여 외부에서 강제로 버퍼 내용을 수정할 경우 사용. 
	//
	// Parameters: (int) 이동 사이즈.
	// Return: (int) 이동된 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int		MoveWritePos(int iSize);
	int		MoveReadPos(int iSize);

	//////////////////////////////////////////////////////////////////////////
	// 데이타 얻기.
	//
	// Parameters: (char *)Dest 포인터. (int)Size.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int GetData(char* chpData, int iSize);

	//////////////////////////////////////////////////////////////////////////
	// 데이타 삽입.
	//
	// Parameters: (char *)Src 포인터. (int)SrcSize.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int PutData(char* chpData, int iSize);

	//////////////////////////////////////////////////////////////////////////
	// 패킷 헤더 사이즈 예약
	//
	// Parameters: 없음.
	// Return: (int) 헤더 크기
	//////////////////////////////////////////////////////////////////////////
	inline void ReserveHeadSize(int iSize) { _iUseSize += iSize; _iRear += iSize; }

	//////////////////////////////////////////////////////////////////////////
	// 예약된 헤더 공간에 데이터 삽입
	//
	// Parameters: (char*)헤더 포인터, (int) 헤더 크기
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	inline void InputHeadData(char* pHeadData, int iSize) { memcpy(&_buffer[0], pHeadData, iSize); }

	//////////////////////////////////////////////////////////////////////////
	// 로그 남기기.
	//
	// Parameters: 없음
	// Return: (char*) 파일 경로 (int)현재 Line, (BYTE) 자료형 구분 타입
	//////////////////////////////////////////////////////////////////////////
	void	MakeLogFile(const WCHAR* path, int iLine, BYTE bType);

	//////////////////////////////////////////////////////////////////////////
	// MemoryPool에서 직렬화버퍼 포인터 할당
	//
	// Parameters: 없음
	// Return: (Packet*) 직렬화 버퍼 포인터
	//////////////////////////////////////////////////////////////////////////
	static Packet* Alloc();

	//////////////////////////////////////////////////////////////////////////
	// 직렬화버퍼 MemoryPool에 반환
	//
	// Parameters: (Packet*) 직렬화 버퍼 포틴터
	// Return: 없음
	//////////////////////////////////////////////////////////////////////////
	static void Free(Packet* pPacket);

	//////////////////////////////////////////////////////////////////////////
	// 참조카운트 1 증가
	//
	// Parameters: 없음.
	// Return: 없음
	//////////////////////////////////////////////////////////////////////////
	inline void AddRef() { InterlockedIncrement(_refCount); }

	//////////////////////////////////////////////////////////////////////////
	// 참조카운트 1 감소
	//
	// Parameters: 없음
	// Return: 참조카운트 1 감소 값
	//////////////////////////////////////////////////////////////////////////
	inline long SubRef() { return InterlockedDecrement(_refCount); }
	
	Packet& operator=(const Packet& ref) = delete;

	Packet& operator<<(unsigned char data);
	Packet& operator<<(char data);
	Packet& operator<<(unsigned short data);
	Packet& operator<<(short data);
	Packet& operator<<(unsigned int data);
	Packet& operator<<(int data);
	Packet& operator<<(unsigned long data);
	Packet& operator<<(long data);
	Packet& operator<<(unsigned __int64 data);
	Packet& operator<<(__int64 data);
	Packet& operator<<(float data);
	Packet& operator<<(double data);


	Packet& operator>>(unsigned char& data)		throw(PacketOutputException);
	Packet& operator>>(char& data)				throw(PacketOutputException);
	Packet& operator>>(unsigned short& data)	throw(PacketOutputException);
	Packet& operator>>(short& data)				throw(PacketOutputException);
	Packet& operator>>(unsigned int& data)		throw(PacketOutputException);
	Packet& operator>>(int& data)				throw(PacketOutputException);
	Packet& operator>>(unsigned long& data)		throw(PacketOutputException);
	Packet& operator>>(long& data)				throw(PacketOutputException);
	Packet& operator>>(unsigned __int64& data)	throw(PacketOutputException);
	Packet& operator>>(__int64& data)			throw(PacketOutputException);
	Packet& operator>>(float& data)				throw(PacketOutputException);
	Packet& operator>>(double& data)			throw(PacketOutputException);

private:
	//////////////////////////////////////////////////////////////////////////
	// 버퍼 크기 증가.
	//
	// Parameters: 없음
	// Return: 없음
	//////////////////////////////////////////////////////////////////////////
	void Resize(void);

	// TEMP PUBLIC
	// ORIGIN PRIVATE
public:
	char* _buffer = nullptr;
	
	int _iUseSize;
	int _iFront;
	int _iRear;
	int _iBufferSize;
	long* _refCount;

	alignas(64) bool _bOnEncodeFlag;

public:
	static MemoryPoolTLS<Packet> _packetPool;
	static uint8 _packetKey;
	static uint8 _packetCode;
};

inline void Packet::Clear(void)
{
	_iFront = 0;
	_iRear = 0;
	_iUseSize = 0;
}