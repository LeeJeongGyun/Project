#include "pch.h"
#include "RingBuffer.h"

RingBuffer::RingBuffer()
	:_iBufferSize(BUFFER_SIZE), _iFront(0), _iRear(0)
{
	_buffer = new char[BUFFER_SIZE];
}

RingBuffer::RingBuffer(int iBufferSize)
	: _iBufferSize(iBufferSize), _iFront(0), _iRear(0)
{
	_buffer = new char[iBufferSize];
}

RingBuffer::~RingBuffer()
{
	if (_buffer != nullptr)
		delete[] _buffer;

	_buffer = nullptr;
}

int RingBuffer::Enqueue(const char* ptr, int iSize)
{
	// UseSize È®ÀÎ
	int iEnqueSize = 0;

	int iRear = _iRear;
	int iFront = _iFront;

	if (iFront == -1)
		iEnqueSize = _iBufferSize - iRear - 1;
	else if (iRear >= iFront)
		iEnqueSize = (_iBufferSize - 1) - iRear + iFront;
	else
		iEnqueSize = iFront - iRear - 1;

	if (iEnqueSize >= iSize)
		iEnqueSize = iSize;
	
	if (iEnqueSize + iRear >= _iBufferSize)
	{
		int iDirectSize = _iBufferSize - iRear - 1;
		memcpy(&_buffer[iRear + 1], ptr, iDirectSize);
		memcpy(&_buffer[0], &ptr[iDirectSize], iEnqueSize - iDirectSize);
		iRear = iEnqueSize - iDirectSize - 1;
	}
	else
	{
		memcpy(&_buffer[iRear + 1], ptr, iEnqueSize);
		iRear += iEnqueSize;
	}
	_iRear = iRear;

	return iEnqueSize;
}

int RingBuffer::Dequeue(char* ptr, int iSize)
{
	int iDequeSize = 0;

	int iRear = _iRear;
	int iFront = _iFront;

	if (iRear == -1)
		iDequeSize = _iBufferSize - iFront - 1;
	if (iRear >= iFront)
		iDequeSize = iRear - iFront;
	else
		iDequeSize = _iBufferSize - iFront + iRear;

	if (iSize < iDequeSize)
		iDequeSize = iSize;

	if (iDequeSize + iFront >= _iBufferSize)
	{
		int iDirectSize = _iBufferSize - iFront - 1;
		memcpy(ptr, &_buffer[iFront + 1], iDirectSize);
		memcpy(&ptr[iDirectSize], &_buffer[0], iDequeSize - iDirectSize);
		iFront = iDequeSize - iDirectSize - 1;
	}
	else
	{
		memcpy(ptr, &_buffer[iFront + 1], iDequeSize);
		iFront += iDequeSize;
	}
	_iFront = iFront;

	return iDequeSize;
}

int RingBuffer::Peek(char* ptr, int iSize)
{
	int iDequeSize = 0;
	int iRear = _iRear;
	int iFront = _iFront;


	if (iRear >= iFront)
		iDequeSize = iRear - iFront;
	else
		iDequeSize = _iBufferSize - iFront + iRear;

	if (iSize < iDequeSize)
		iDequeSize = iSize;

	if (iDequeSize + iFront >= _iBufferSize)
	{
		int iDirectSize = _iBufferSize - iFront - 1;
		memcpy(ptr, &_buffer[iFront + 1], iDirectSize);
		memcpy(&ptr[iDirectSize], &_buffer[0], iDequeSize - iDirectSize);
	}
	else
	{
		memcpy(ptr, &_buffer[iFront + 1], iDequeSize);
	}

	return iDequeSize;
}

int RingBuffer::DirectEnqueueSize(void)
{
	if (_iRear >= _iBufferSize - 1)
		_iRear = -1;

	if (_iFront <= _iRear)
		return _iBufferSize - _iRear - 1;
	else
		return _iFront - _iRear - 1;
}

int RingBuffer::DirectDequeueSize(void)
{
	if (_iFront >= _iBufferSize - 1)
		_iFront = -1;

	if (_iFront > _iRear)
		return _iBufferSize - _iFront - 1;
	else
		return _iRear - _iFront;
}

void RingBuffer::MoveRear(int iSize)
{
	// TEST
	int iRear = _iRear;

	if (iSize >= _iBufferSize)
		return;

	if (iRear + iSize >= _iBufferSize)
		iRear = iRear + iSize - _iBufferSize;
	else
		iRear += iSize;
		
	_iRear = iRear;


	//if (_iRear + iSize >= _iBufferSize)
	//	_iRear = _iRear + iSize - _iBufferSize;
	//else
	//	_iRear += iSize;
}

void RingBuffer::MoveFront(int iSize)
{
	// TEST
	int iFront = _iFront;

	if (iSize >= _iBufferSize)
		return;

	if (iFront + iSize >= _iBufferSize)
		iFront = iFront + iSize - _iBufferSize;
	else
		iFront += iSize;

	_iFront = iFront;


	//if (_iFront + iSize >= _iBufferSize)
	//	_iFront = _iFront + iSize - _iBufferSize;
	//else
	//	_iFront += iSize;
}

void RingBuffer::ClearBuffer(void)
{
	_iFront = 0;
	_iRear = 0;
}

int RingBuffer::GetUseSize() 
{
	/*int iRear = _iRear;
	int iFront = _iFront;

	if (iRear == -1)
		return _iBufferSize - iFront - 1;
	if (iRear >= iFront)
		return iRear - iFront;
	else
		return _iBufferSize - iFront + iRear;*/

	int iRear = _iRear;
	int iFront = _iFront;

	if (_iRear == -1)
		return _iBufferSize - _iFront - 1;
	if (_iRear >= _iFront)
		return _iRear - _iFront;
	else
		return _iBufferSize - _iFront + _iRear;

}

int RingBuffer::GetFreeSize() const
{
	int iRear = _iRear;
	int iFront = _iFront;

	if (iFront == -1)
		return _iBufferSize - iRear - 1;
	else if (iRear >= iFront)
		return (_iBufferSize - 1) - iRear + iFront;
	else
		return iFront - iRear - 1;
}

void RingBuffer::ReSize(int iSize)
{
	if (_buffer != nullptr)
		delete[] _buffer;

	_buffer = new char[iSize];
	_iBufferSize = iSize;
	_iFront = 0;
	_iRear = 0;
}