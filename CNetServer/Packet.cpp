#include "pch.h"
#include "Packet.h"

MemoryPoolTLS <Packet> Packet::_packetPool;

Packet::Packet()
	:_iUseSize(0), _iBufferSize(eBUFFER_DEFAULT), _iFront(0), _iRear(0)
{
	_buffer = new char[eBUFFER_DEFAULT];
	_refCount = new long(1);
	_bOnEncodeFlag = false;
}

Packet::Packet(int iBufferSize)
	: _iUseSize(0), _iBufferSize(iBufferSize), _iFront(0), _iRear(0)
{
	_buffer = new char[iBufferSize];
	_refCount = new long(1);
	_bOnEncodeFlag = false;
}

Packet::~Packet()
{
	Release();
}

void Packet::Release(void)
{
	if (_buffer != nullptr)
		delete[] _buffer;

	_buffer = nullptr;
	delete _refCount;
}

void Packet::Encode()
{
	if (_bOnEncodeFlag)
		return;

	// 정해진 코드값, 길이, 랜덤 키, 체크섬, 페이로드
	int16 payloadSize = GetUseSize() - 5; // 헤더크기를 뺀 데이터 사이즈

	char* ptr = _buffer + 5;
	int32 sumOfPayload = 0;
	uint8 checkSum, Key = _packetKey;
	uint8 RKey = rand() % 256;

	// TODO 
	// 세팅 함수로 빼자 
	// 원래 체크섬
	for (int iCnt = 0; iCnt < payloadSize; iCnt++)
	{
		sumOfPayload += *(ptr + iCnt);
	}

	checkSum = sumOfPayload % 256;

	// MakePacketHeader 이런 세팅함수 만들어서 빼놓자
	// *(_buffer) = code;
	// *((uint16*)(_buffer + 1)) = payloadSize;
	// *(_buffer + 3) = RKey;
	// *(_buffer + 4) = checkSum;
	// code(1), len(2), RKey(1), checksum(1) 이거 헤더에 넣어준다.

	//////////////////////////////////////////////////////////////////////////
	_iRear = 0;
	_iUseSize = 0;
	_iFront = 0;
	*this << _packetCode << payloadSize << RKey;

	//////////////////////////////////////////////////////////////////////////

	uint8 P, E, BP, BE;

	P = checkSum ^ (RKey + 1);
	E = P ^ (Key + 1);

	*this << E;
	BP = P;
	BE = E;

	for (int32 iCnt = 0; iCnt < payloadSize; iCnt++)
	{
		P = *(ptr + iCnt) ^ (BP + RKey + iCnt + 2);
		E = P ^ (BE + Key + iCnt + 2);
		BP = P;
		BE = E;

		*this << E;
	}

	_bOnEncodeFlag = true;
}

bool Packet::Decode()
{
	uint8 code, RKey, Key = _packetKey, checkSum;
	int16 len, payloadSize;
	int32 compareCheckSum = 0;

	*this >> code >> len >> RKey;

	// 코드 값 에러
	if (code != Packet::_packetCode)
	{
		Logger::GetInstance()->Log(L"DECODE_ERROR", eLOG_ERROR, L"Packet Code Error, Code: %d\n", code);
		return false;
	}

	// 데이터 크기 에러
	if (len < 12)
	{
		Logger::GetInstance()->Log(L"DECODE_ERROR", eLOG_ERROR, L"Packet Data Size Error: %d\n", len);
		return false;
	}

	payloadSize = len; // 데이터 사이즈

	char* ptr = _buffer + 4;

	uint8 beforePlainData, plainData, originData;

	plainData = *ptr ^ Key + 1;
	originData = plainData ^ RKey + 1;

	checkSum = originData;
	beforePlainData = plainData;

	_iUseSize = 0;
	_iRear = 0;
	_iFront = 0;

	for (int iCnt = 1; iCnt <= payloadSize; iCnt++)
	{
		plainData = *(ptr + iCnt) ^ *(ptr + iCnt - 1) + Key + iCnt + 1;
		originData = plainData ^ (beforePlainData + RKey + iCnt + 1);
		beforePlainData = plainData;

		*this << originData;
		compareCheckSum += originData;
	}

	compareCheckSum %= 256;

	// 체크섬 에러
	if (compareCheckSum != checkSum)
	{
		Logger::GetInstance()->Log(L"DECODE_ERROR", eLOG_ERROR, L"CheckSum Error, CheckSum: %d\n", compareCheckSum);
		return false;
	}

	return true;
}


int Packet::MoveWritePos(int iSize)
{
	if (_iRear + iSize > _iBufferSize)
	{
		_iRear = _iBufferSize;
		return _iBufferSize - _iRear;
	}

	_iRear += iSize;
	_iUseSize += iSize;
	return iSize;
}

int Packet::MoveReadPos(int iSize)
{
	if (_iFront + iSize > _iRear)
	{
		_iFront = _iRear;
		return _iRear - _iFront;
	}

	_iFront += iSize;
	_iUseSize -= iSize;
	return iSize;
}

int Packet::GetData(char* chpData, int iSize)
{
	if (iSize > _iUseSize)
	{
		// 말이 안되는 상황 발생
		// 크래쉬 때리자
		return 0;
	}

	memcpy(chpData, &_buffer[_iFront], iSize);

	_iFront += iSize;
	_iUseSize -= iSize;

	return iSize;
}

int Packet::PutData(char* chpData, int iSize)
{
	if (_iRear + iSize > _iBufferSize)
	{
		// 로그 남기고 사이즈 키워야지
		Resize();
	}

	memcpy(&_buffer[_iRear], chpData, iSize);
	_iRear += iSize;
	_iUseSize += iSize;

	return iSize;
}

void Packet::Resize(void)
{
	_iBufferSize += _iBufferSize / 2;

	char* temp;
	temp = new char[_iBufferSize];
	memcpy(temp, _buffer, _iRear);

	delete[] _buffer;
	_buffer = temp;
}

void Packet::MakeLogFile(const WCHAR* path, int iLine, BYTE bType)
{
	FILE* pLogFile;
	errno_t err = _wfopen_s(&pLogFile, L"Packet_Input_Memory_Corruption.txt", L"at");
	if (err != 0) { return; } // CRASH
	if (pLogFile == nullptr) { return; } // CRASH

	switch (bType)
	{
	case enUCHAR:
		fwprintf(pLogFile, L"Packet::operator<<(unsigned char)\n");
		break;
	case enCHAR:
		fwprintf(pLogFile, L"Packet::operator<<(char)\n");
		break;
	case enUSHORT:
		fwprintf(pLogFile, L"Packet::operator<<(unsigned short)\n");
		break;
	case enSHORT:
		fwprintf(pLogFile, L"Packet::operator<<(short)\n");
		break;
	case enUINT:
		fwprintf(pLogFile, L"Packet::operator<<(unsigned int)\n");
		break;
	case enINT:
		fwprintf(pLogFile, L"Packet::operator<<(int)\n");
		break;
	case enINT64:
		fwprintf(pLogFile, L"Packet::operator<<(int64)\n");
		break;
	case enUINT64:
		fwprintf(pLogFile, L"Packet::operator<<(unsigned int64)\n");
		break;
	case enLONG:
		fwprintf(pLogFile, L"Packet::operator<<(long)\n");
		break;
	case enULONG:
		fwprintf(pLogFile, L"Packet::operator<<(unsigned long)\n");
		break;
	case enFLOAT:
		fwprintf(pLogFile, L"Packet::operator<<(float)\n");
		break;
	case enDOUBLE:
		fwprintf(pLogFile, L"Packet::operator<<(double)\n");
		break;
	default:
		fwprintf(pLogFile, L"Packet::PutData(const char*,int)\n");
	}

	fwprintf(pLogFile, L"Path: %s\n", path);
	fwprintf(pLogFile, L"Line: %d\n", iLine);
	fwprintf(pLogFile, L"Data: ");

	for (int i = _iFront; i < _iRear; i++)
	{
		fwprintf(pLogFile, L"%x ", _buffer[i]);
	}
	fwprintf(pLogFile, L"\n");

	fclose(pLogFile);
}

Packet* Packet::Alloc()
{
	Packet* pPacket = _packetPool.Alloc();
	pPacket->_iFront = 0;
	pPacket->_iRear = 0;
	pPacket->_iUseSize = 0;
	*(pPacket->_refCount) = 1;
	
	return pPacket;
}

void Packet::Free(Packet* pPacket)
{
	pPacket->_bOnEncodeFlag = false;
	_packetPool.Free(pPacket);
}

Packet& Packet::operator<<(unsigned char data)
{
	if (_iRear + sizeof(unsigned char) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enUCHAR);
		Resize();
	}

	*((unsigned char*)&_buffer[_iRear]) = data;

	// MoveWritePos 를 사용할 수도 있지만
	// 패킷에 집어 넣는 이 함수는 자주 사용되기 때문에
	// 함수 사용하지 않는다.
	_iRear += sizeof(unsigned char);
	_iUseSize += sizeof(unsigned char);
	return *this;
}

Packet& Packet::operator<<(char data)
{
	if (_iRear + sizeof(char) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enCHAR);
		Resize();
	}

	*((unsigned char*)&_buffer[_iRear]) = data;

	_iRear += sizeof(char);
	_iUseSize += sizeof(char);
	return *this;
}

Packet& Packet::operator<<(unsigned short data)
{
	if (_iRear + sizeof(unsigned short) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enUSHORT);
		Resize();
	}

	*((unsigned short*)&_buffer[_iRear]) = data;

	_iRear += sizeof(unsigned short);
	_iUseSize += sizeof(unsigned short);
	return *this;
}

Packet& Packet::operator<<(short data)
{
	if (_iRear + sizeof(short) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enSHORT);
		Resize();
	}

	*((short*)&_buffer[_iRear]) = data;

	_iRear += sizeof(short);
	_iUseSize += sizeof(short);
	return *this;
}

Packet& Packet::operator<<(unsigned int data)
{
	if (_iRear + sizeof(unsigned int) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enUINT);
		Resize();
	}

	*((unsigned int*)&_buffer[_iRear]) = data;

	_iRear += sizeof(unsigned int);
	_iUseSize += sizeof(unsigned int);
	return *this;
}

Packet& Packet::operator<<(int data)
{
	if (_iRear + sizeof(int) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enINT);
		Resize();
	}

	*((int*)&_buffer[_iRear]) = data;

	_iRear += sizeof(int);
	_iUseSize += sizeof(int);
	return *this;
}

Packet& Packet::operator<<(unsigned long data)
{
	if (_iRear + sizeof(unsigned long) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enULONG);
		Resize();
	}

	*((unsigned long*)&_buffer[_iRear]) = data;

	_iRear += sizeof(unsigned long);
	_iUseSize += sizeof(unsigned long);
	return *this;
}

Packet& Packet::operator<<(long data)
{
	if (_iRear + sizeof(long) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enLONG);
		Resize();
	}

	*((long*)&_buffer[_iRear]) = data;

	_iRear += sizeof(long);
	_iUseSize += sizeof(long);
	return *this;
}

Packet& Packet::operator<<(unsigned __int64 data)
{
	if (_iRear + sizeof(unsigned __int64) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enUINT64);
		Resize();
	}

	*((unsigned __int64*)&_buffer[_iRear]) = data;

	_iRear += sizeof(unsigned __int64);
	_iUseSize += sizeof(unsigned __int64);
	return *this;
}

Packet& Packet::operator<<(__int64 data)
{
	if (_iRear + sizeof(__int64) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enINT64);
		Resize();
	}

	*((__int64*)&_buffer[_iRear]) = data;

	_iRear += sizeof(__int64);
	_iUseSize += sizeof(__int64);
	return *this;
}

Packet& Packet::operator<<(float data)
{
	if (_iRear + sizeof(float) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enFLOAT);
		Resize();
	}

	*((float*)&_buffer[_iRear]) = data;

	_iRear += sizeof(float);
	_iUseSize += sizeof(float);
	return *this;
}

Packet& Packet::operator<<(double data)
{
	if (_iRear + sizeof(double) > _iBufferSize)
	{
		MakeLogFile(__FILEW__, __LINE__, enDOUBLE);
		Resize();
	}

	*((double*)&_buffer[_iRear]) = data;

	_iRear += sizeof(double);
	_iUseSize += sizeof(double);
	return *this;
}


Packet& Packet::operator>>(unsigned char& data)
{
	if (_iFront + sizeof(unsigned char) > _iRear)
		throw PacketOutputException(sizeof(unsigned char), _buffer, _iRear, __LINE__);

	data = *(unsigned char*)&_buffer[_iFront];
	_iFront += sizeof(unsigned char);
	_iUseSize -= sizeof(unsigned char);
	return *this;
}

Packet& Packet::operator>>(char& data)
{
	if (_iFront + sizeof(char) > _iRear)
		throw PacketOutputException(sizeof(char), _buffer, _iRear, __LINE__);

	data = *(char*)&_buffer[_iFront];
	_iFront += sizeof(char);
	_iUseSize -= sizeof(char);
	return *this;
}

Packet& Packet::operator>>(unsigned short& data)
{
	if (_iFront + sizeof(unsigned short) > _iRear)
		throw PacketOutputException(sizeof(unsigned short), _buffer, _iRear, __LINE__);

	data = *(unsigned short*)&_buffer[_iFront];
	_iFront += sizeof(unsigned short);
	_iUseSize -= sizeof(unsigned short);
	return *this;
}

Packet& Packet::operator>>(short& data)
{
	if (_iFront + sizeof(short) > _iRear)
		throw PacketOutputException(sizeof(short), _buffer, _iRear, __LINE__);

	data = *(short*)&_buffer[_iFront];
	_iFront += sizeof(short);
	_iUseSize -= sizeof(short);
	return *this;
}

Packet& Packet::operator>>(unsigned int& data)
{
	if (_iFront + sizeof(unsigned int) > _iRear)
		throw PacketOutputException(sizeof(unsigned int), _buffer, _iRear, __LINE__);

	data = *(unsigned int*)&_buffer[_iFront];
	_iFront += sizeof(unsigned int);
	_iUseSize -= sizeof(unsigned int);
	return *this;
}

Packet& Packet::operator>>(int& data)
{
	if (_iFront + sizeof(int) > _iRear)
		throw PacketOutputException(sizeof(int), _buffer, _iRear, __LINE__);

	data = *(int*)&_buffer[_iFront];
	_iFront += sizeof(int);
	_iUseSize -= sizeof(int);
	return *this;
}

Packet& Packet::operator>>(unsigned long& data)
{
	if (_iFront + sizeof(unsigned long) > _iRear)
		throw PacketOutputException(sizeof(unsigned long), _buffer, _iRear, __LINE__);

	data = *(unsigned long*)&_buffer[_iFront];
	_iFront += sizeof(unsigned long);
	_iUseSize -= sizeof(unsigned long);
	return *this;
}

Packet& Packet::operator>>(long& data)
{
	if (_iFront + sizeof(long) > _iRear)
		throw PacketOutputException(sizeof(long), _buffer, _iRear, __LINE__);

	data = *(long*)&_buffer[_iFront];
	_iFront += sizeof(long);
	_iUseSize -= sizeof(long);
	return *this;
}

Packet& Packet::operator>>(unsigned __int64& data)
{
	if (_iFront + sizeof(unsigned __int64) > _iRear)
		throw PacketOutputException(sizeof(unsigned __int64), _buffer, _iRear, __LINE__);

	data = *(unsigned __int64*)&_buffer[_iFront];
	_iFront += sizeof(unsigned __int64);
	_iUseSize -= sizeof(unsigned __int64);
	return *this;
}

Packet& Packet::operator>>(__int64& data)
{
	if (_iFront + sizeof(__int64) > _iRear)
		throw PacketOutputException(sizeof(__int64), _buffer, _iRear, __LINE__);

	data = *(__int64*)&_buffer[_iFront];
	_iFront += sizeof(__int64);
	_iUseSize -= sizeof(__int64);
	return *this;
}

Packet& Packet::operator>>(float& data)	throw(PacketOutputException)
{
	if (_iFront + sizeof(float) > _iRear)
		throw PacketOutputException(sizeof(float), _buffer, _iRear, __LINE__);

	data = *(float*)&_buffer[_iFront];
	_iFront += sizeof(float);
	_iUseSize -= sizeof(float);
	return *this;
}

Packet& Packet::operator>>(double& data) throw(PacketOutputException)
{
	if (_iFront + sizeof(double) > _iRear)
		throw PacketOutputException(sizeof(double), _buffer, _iRear, __LINE__);

	data = *(double*)&_buffer[_iFront];
	_iFront += sizeof(double);
	_iUseSize -= sizeof(double);
	return *this;
}