#pragma once

#include<Windows.h>
#include<iostream>
#include "MemoryPool.h"
#include "Chunk.h"

template<typename DATA>
class MemoryPoolTLS
{
public:
	//////////////////////////////////////////////////////////////////////////
	// 생성자, 소멸자
	//
	// Parameters:	(bool) Alloc 시 생성자 / Free 시 소멸자 호출 여부
	//////////////////////////////////////////////////////////////////////////
	MemoryPoolTLS(bool bPlacementFlag = false);
	~MemoryPoolTLS();

	//////////////////////////////////////////////////////////////////////////
	// Chunk 내부 데이터 할당
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이터 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	DATA*			Alloc();

	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 Chunk 내부 데이터 해제
	// Chunk에 있는 모든 데이터 전부 해제됐다면 MemoryPool로 Free
	//
	// Parameters: (DATA *) 데이터 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool			Free(DATA* pData);

	//////////////////////////////////////////////////////////////////////////
	// 현재 확보 된 Chunk 개수를 얻는다. (메모리풀 내부의 전체 개수)
	//
	// Parameters: 없음.
	// Return: (LONG64) 메모리 풀 내부 할당해준 Chunk 개수
	//////////////////////////////////////////////////////////////////////////
	long			GetUseCount() { return _useCount; }

private:
	// Chunk를 관리하는 메모리 풀
	MemoryPool<Chunk<DATA>>		_memoryPool;

	// Chunk 사용 개수 
	long						_useCount;

	// TLS 인덱스 저장 
	// _tlsIndex랑 _placementFlag는 한번 데이터 저장하고 load만 함으로 캐시라인 만큼 떨어뜨려
	// 캐시 미스날 확률을 줄여준다.
	alignas(64) DWORD			_tlsIndex;

	// PlacementNew 사용 플래그
	bool						_bPlacementFlag;
};

template<typename DATA>
inline MemoryPoolTLS<DATA>::MemoryPoolTLS(bool bPlacementFlag)
	: _useCount(0), _memoryPool(true, bPlacementFlag), _bPlacementFlag(bPlacementFlag)
{
	_tlsIndex = TlsAlloc();
	if (_tlsIndex == TLS_OUT_OF_INDEXES)
	{
		// TODO
		// CRASH & LOG
	}
}

template<typename DATA>
inline MemoryPoolTLS<DATA>::~MemoryPoolTLS()
{

}

template<typename DATA>
inline DATA* MemoryPoolTLS<DATA>::Alloc()
{
	Chunk<DATA>* pChunk;

	// 어셈블리 코드
	DWORD tlsIndex = _tlsIndex;
	long allocCount;

	if (TlsGetValue(tlsIndex) == 0)
	{
		pChunk = _memoryPool.Alloc();
		TlsSetValue(tlsIndex, pChunk);

		_InterlockedIncrement(&_useCount);

		// 어셈블리 코드
		allocCount = pChunk->_allocCount++;

		if (allocCount == dfCHUNK_SIZE - 1)
		{
			TlsSetValue(tlsIndex, 0);
		}
	}
	else
	{
		pChunk = (Chunk<DATA>*)TlsGetValue(tlsIndex);

		// 어셈블리 코드
		allocCount = pChunk->_allocCount++;

		if (allocCount == dfCHUNK_SIZE - 1)
		{
			TlsSetValue(tlsIndex, 0);
		}
	}

	if (_bPlacementFlag)
	{
		new (&pChunk->node[allocCount].data) DATA;
	}

	return &pChunk->node[allocCount].data;
}

template<typename DATA>
inline bool MemoryPoolTLS<DATA>::Free(DATA* pData)
{
	Chunk<DATA>* pChunk = ((typename Chunk<DATA>::ChunkNode*)pData)->pChunk;

#ifndef _PERFORMANCE
	if (pChunk->guard != ((typename Chunk<DATA>::ChunkNode*)pData)->guard)
	{
		// CRASH
		// LOG
		wprintf(L"Guard Fail\n");
		return false;
	}
#endif

	if (_bPlacementFlag)
	{
		pData->~DATA();
	}

	if (dfCHUNK_SIZE == InterlockedIncrement(&pChunk->_freeCount))
	{
		_memoryPool.Free(pChunk);
		_InterlockedDecrement(&_useCount);
	}

	return true;
}