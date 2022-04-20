#pragma once

#include<Windows.h>
#include "Macro.h"

template<typename DATA>
class MemoryPool
{
	struct Node;

	// NOTE
	// Alloc 함수에서 CountedNode를 지역변수로 선언하고 있다.
	// DoubleCAS에 주소값 집어넣으므로 16Byte 정렬 필수
	struct alignas(16) CountedNode
	{
		// NOTE 
		// Alloc 함수에서 CountedNode의 지역변수와 클래스의 멤버와 
		// 구조체 대입을 할 경우 순서에 유의해야한다.
		// uniqueCount가 nodePtr 보다 먼저 대입될 경우
		// Free에서는 속도를 높히기 위해 DoubleCAS를 사용하지 않으므로
		// Alloc쪽에서 Free를 알지 못해 일어나는 문제가 발생한다.
		LONG64 uniqueCount;
		Node* nodePtr;
	};

#ifdef _PERFORMANCE
	struct Node
	{
		inline void PlacementNewInit() {}
		inline void Init() {}
		DATA data;
		Node* next;
	};
#else 
	struct Node
	{
		inline void PlacementNewInit() {}
		inline void Init() {}
		DATA data;
		ULONG64 guard;
		Node* next;
	};
#endif

public:
	//////////////////////////////////////////////////////////////////////////
	// 생성자, 소멸자
	//
	// Parameters:	(bool) Alloc 시 생성자 / Free 시 소멸자 호출 여부
	//////////////////////////////////////////////////////////////////////////
	MemoryPool(bool bPlacementNew = false, bool bTLSPlacementNew = false);
	~MemoryPool();

	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.  
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	DATA* Alloc();

	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool			Free(DATA* ptr);

	//////////////////////////////////////////////////////////////////////////
	// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
	//
	// Parameters: 없음.
	// Return: (LONG64) 메모리 풀 내부 전체 개수
	//////////////////////////////////////////////////////////////////////////
	LONG64			GetAllocCount() { return _allocCount; }

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 블럭 개수를 얻는다.
	//
	// Parameters: 없음.
	// Return: (int) 사용중인 블럭 개수.
	//////////////////////////////////////////////////////////////////////////
	LONG64			GetUseCount() { return _allocCount - _nodeCount; }

private:
	// 내부에서 관리하는 스택의 top을 가리키는 포인터
	CountedNode* _top;

	// NOTE
	// _top, _allocCount, _nodeCount 전부 Interlocked 함수 사용
	// 따라서 동일 캐시라인에 존재하게 된다면 서로 연관이 없음에도 메모리에 접근하기 위해
	// 하드웨어 적으로 대기하는 현상이 발생할 수 있다.
	// 따라서 alignas를 사용하여 캐시라인만큼 띄우는 방법 선택했다.

	// 총 할당해준 블럭의 개수
	alignas(64) DWORD	_allocCount;

	// 현재 내부에서 들고 있는 블럭의 개수
	alignas(64) long	_nodeCount;

	// Placement New 사용 플래그
	alignas(64) bool	_bPlacementNew;

	// TLS에서 MemoryPool을 사용했을 때 TLSMemoryPool의 Placement New 사용 플래그
	bool				_bTLSPlacementNew;

#ifndef _PERFORMANCE
	// 내 오브젝트 풀에서 Alloc 해준 메모리가 맞는지 확인
	ULONG64				_guard;
#endif
};

template<typename DATA>
inline MemoryPool<DATA>::MemoryPool(bool bPlacementNew, bool bTLSPlacementNew)
	:_allocCount(0), _nodeCount(0), _bPlacementNew(bPlacementNew), _bTLSPlacementNew(bTLSPlacementNew)
{
	_top = (CountedNode*)_aligned_malloc(sizeof(CountedNode), 16);
	_top->nodePtr = nullptr;
	_top->uniqueCount = 0;

#ifndef _PERFORMANCE
	_guard = g_Guard;
	g_Guard++;
#endif
}

template<typename DATA>
inline MemoryPool<DATA>::~MemoryPool()
{
	Node* top = _top->nodePtr;
	_aligned_free(_top);

	while (top)
	{
		Node* dNode = top;
		top = dNode->next;
		if (_bPlacementNew)
		{
			free(dNode);
		}
		else
		{
			delete dNode;
		}
	}
}

template<typename DATA>
inline DATA* MemoryPool<DATA>::Alloc()
{
	CountedNode popTop;
	Node* retNode = nullptr;

	// NOTE
	// 애초에 없으면 Interlocked 함수를 사용하지 않음으로써
	// 경합 발생 가능성 줄여준다.
	if (_nodeCount > 0)
	{
		if (_InterlockedDecrement(&_nodeCount) >= 0)
		{
			do
			{
				// NOTE
				// popTop = *_top;
				// Compile이 byte단위 복사로 나온다면 문제가 생길 가능성이 존재한다.
				// 따라서 멤버간 대입연산으로 바꿈
				popTop.uniqueCount = _top->uniqueCount;
				popTop.nodePtr = _top->nodePtr;
			} while (false == _InterlockedCompareExchange128((LONG64*)_top, (LONG64)popTop.nodePtr->next, popTop.uniqueCount + 1, (LONG64*)&popTop));

			retNode = popTop.nodePtr;
		}
		else
		{
			_InterlockedIncrement(&_nodeCount);
		}
	}

	if (retNode == nullptr)
	{
		if (_bPlacementNew)
		{
			retNode = (Node*)malloc(sizeof(Node));
			new (&retNode->data) DATA;

			if (_bTLSPlacementNew)
			{
				retNode->data.PlacementNewInit();
			}
			else
			{
				retNode->data.Init();
			}
		}
		else
		{
			retNode = new Node;

			// TLSMemoryPool 사용 시 내부 메모리풀은 무조건 PlacementNew를 사용하기 때문에
			// 여기에 TLS 관련 분기문은 들어갈 필요가 없다.
		}

		_InterlockedIncrement(&_allocCount);
	}
	else
	{
		if (_bPlacementNew)
		{
			new (&retNode->data) DATA;
		}
	}

#ifndef _PERFORMANCE
	retNode->guard = _guard;
#endif

	return (DATA*)retNode;
}

template<typename DATA>
inline bool MemoryPool<DATA>::Free(DATA* ptr)
{
	Node* newTop = (Node*)ptr;
	Node* nowTop;

	if (_bPlacementNew)
	{
		ptr->~DATA();
	}

#ifndef _PERFORMANCE
	if (newTop->guard != _guard)
	{
		// CRASH
		return false;
	}
#endif

	// NOTE
	// _InterlockedCompareExchange128은 _InterlockedCompareExchange64보다 더 느리다.
	// 따라서 Free부분에서는 128 사용하지 않고 64를 채택함
	// 그로인해서 Alloc쪽 Node Pop하는 부분에서 발생할 수 있는 문제점 주의해야한다.
	do
	{
		nowTop = _top->nodePtr;
		newTop->next = nowTop;
	} while (_InterlockedCompareExchange64((LONG64*)&_top->nodePtr, (LONG64)newTop, (LONG64)nowTop) != (LONG64)nowTop);

	_InterlockedIncrement(&_nodeCount);
	return true;
}