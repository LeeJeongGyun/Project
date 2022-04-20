#pragma once

#include<windows.h>
#include "SMemoryPool.h"

template<typename DATA>
class LockFreeStack
{
	struct Node;

	struct CountedNode
	{
		LONG64 uniqueCount;
		Node* nodePtr;
	};

	struct Node
	{
		DATA pData;
		Node* next;
	};

public:
	LockFreeStack();
	~LockFreeStack();

	void Push(const DATA& pdata);
	bool Pop(DATA& pData);

	int GetUseCount() const { return _useCount; }

private:
	CountedNode* _top;
	SMemoryPool<Node> pool;

	// _top과 _useCount는 상관없는 변수다.
	// 동일 캐시라인에 있으면 Interlocked관련 함수 사용할 때 상관이 없는데 대기하는 경우가 발생할 수 있다.
	alignas(64) long _useCount;
};

template<typename DATA>
inline LockFreeStack<DATA>::LockFreeStack()
	: _useCount(0)
{
	_top = (CountedNode*)_aligned_malloc(sizeof(CountedNode), 16);
	_top->nodePtr = nullptr;
}

template<typename DATA>
inline LockFreeStack<DATA>::~LockFreeStack()
{
	Node* top = _top->nodePtr;
	_aligned_free(_top);

	while (top)
	{
		Node* allocNode = top;
		top = top->next;
		pool.Free(allocNode);
	}
}

template<typename DATA>
inline void LockFreeStack<DATA>::Push(const DATA& data)
{
	Node* pTop;
	Node* pNewNode = pool.Alloc();
	pNewNode->pData = data;

	do {
		pTop = _top->nodePtr;
		pNewNode->next = pTop;
	} while (_InterlockedCompareExchange64((LONG64*)&_top->nodePtr, (LONG64)pNewNode, (LONG64)pTop) != (LONG64)pTop);

	InterlockedIncrement(&_useCount);
}

template<typename DATA>
inline bool LockFreeStack<DATA>::Pop(DATA& pData)
{
	alignas(16) CountedNode popTop;

	// Push 한번하고 Pop을 2개의 스레드가 들어온다면 if(_useCount == 0) 이렇게 하면 문제된다.
	if (InterlockedDecrement(&_useCount) < 0)
	{
		InterlockedIncrement(&_useCount);
		return false;
	}

	do
	{
		// 문제 발생 가능성 존재!! byte단위 대입일때
		//popTop = *_top;
		popTop.uniqueCount = _top->uniqueCount;
		popTop.nodePtr = _top->nodePtr;
	} while (false == _InterlockedCompareExchange128((LONG64*)_top, (LONG64)popTop.nodePtr->next, popTop.uniqueCount + 1, (LONG64*)&popTop));

	pData = popTop.nodePtr->pData;
	pool.Free(popTop.nodePtr);
	return true;
}