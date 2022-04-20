#pragma once

#include<Windows.h>
#include "Macro.h"

template<typename T>
class Chunk
{
public:
	typedef struct ChunkNode
	{
		T data;
		Chunk* pChunk;

#ifndef _PERFORMANCE
		ULONG64 guard;
#endif
	}ChunkNode;

public:
	Chunk();
	~Chunk();

	//////////////////////////////////////////////////////////////////////////
	// ChunkNode 내부 DATA 생성자 호출을 위한 new 할당
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void Init();

	//////////////////////////////////////////////////////////////////////////
	// placementNew를 이용하기 위해서 ChunkNode배열 malloc을 이용해 할당
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void PlacementNewInit();

public:
	ChunkNode* node;
	long _allocCount;
	long _freeCount;

#ifndef _PERFORMANCE
	ULONG64 guard;
#endif
};

template<typename T>
inline Chunk<T>::Chunk()
	:_allocCount(0), _freeCount(0)
{
}

template<typename T>
inline Chunk<T>::~Chunk()
{
}

template<typename T>
inline void Chunk<T>::Init()
{
	node = new ChunkNode[dfCHUNK_SIZE];

#ifndef _PERFORMANCE
	guard = InterlockedIncrement(&g_ChunkGuard);
#endif

	for (int iCnt = 0; iCnt < dfCHUNK_SIZE; iCnt++)
	{
		node[iCnt].pChunk = this;
#ifndef _PERFORMANCE
		node[iCnt].guard = guard;
#endif
	}
}

template<typename T>
inline void Chunk<T>::PlacementNewInit()
{
	node = (ChunkNode*)malloc(sizeof(ChunkNode) * dfCHUNK_SIZE);

#ifndef _PERFORMANCE
	guard = InterlockedIncrement(&g_ChunkGuard);
#endif

	for (int iCnt = 0; iCnt < dfCHUNK_SIZE; iCnt++)
	{
		node[iCnt].pChunk = this;
#ifndef _PERFORMANCE
		node[iCnt].guard = guard;
#endif
	}
}
