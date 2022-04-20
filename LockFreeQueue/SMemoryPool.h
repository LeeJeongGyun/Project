#pragma once

#include<Windows.h>

template<typename DATA>
class SMemoryPool
{
	struct Node;

	// NOTE
	// Alloc 함수에서 CountedNode를 지역변수로 선언하고 있다.
	// DoubleCAS에 포인터 집어넣으므로 16Byte 정렬 필수
	struct alignas(16) CountedNode
	{
		// NOTE 
		// Alloc 함수에서 CountedNode의 지역변수와 클래스의 멤버와 
		// 구조체 대입을 할 경우 순서에 유의해야한다.
		// uniqueCount가 상위바이트에 존재하면 문제 발생 가능성 존재한다.
		LONG64 uniqueCount;
		Node* nodePtr;
	};

	struct Node
	{
		DATA data;
		Node* next;
	};

public:
	SMemoryPool();
	~SMemoryPool();

	DATA*			Alloc();
	void			Free(DATA* ptr);

	LONG64			GetAllocCount() { return _allocCount; }
	int				GetUseCount() { return _useCount; }
	int				GetNodeCount() { return _nodeCount; }

private:
	CountedNode* _top;

	// NOTE
	// _top, _allocCount, _useCount, _nodeCount 전부 Interlocked 함수 사용
	// 따라서 동일 캐시라인에 존재하게 된다면 서로 연관이 없음에도 캐시라인에 접근하기 위해
	// 하드웨어 적으로 대기하는 현상이 발생할 수 있다.
	// 따라서 alignas를 사용하여 캐시라인만큼 띄우는 방법 선택했다.
	alignas(64) LONG64	_allocCount;
	alignas(64) long	_useCount;
	alignas(64) long	_nodeCount;
};

template<typename DATA>
inline SMemoryPool<DATA>::SMemoryPool()
	:_useCount(0), _nodeCount(0), _allocCount(0)
{
	_top = (CountedNode*)_aligned_malloc(sizeof(CountedNode), 16);
	_top->nodePtr = nullptr;
	_top->uniqueCount = 0;
}

template<typename DATA>
inline SMemoryPool<DATA>::~SMemoryPool()
{
	Node* top = _top->nodePtr;
	_aligned_free(_top);

	while (top)
	{
		Node* dNode = top;
		top = dNode->next;
		delete dNode;
	}
}

template<typename DATA>
inline DATA* SMemoryPool<DATA>::Alloc()
{
	CountedNode popTop;
	Node* retNode = nullptr;

	// NOTE
	// 애초에 없으면 _InterlockedDecrement를 하지 않음으로써
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
		retNode = new Node;

		// NOTE
		// 동시에 2개 이상의 스레드가 이 코드를 실행시킬 수 있기 때문에
		// 정확한 AllocCount를 기록하기 위해 Interlocked 사용했다.
		_InterlockedIncrement64(&_allocCount);
	}

	_InterlockedIncrement(&_useCount);
	return (DATA*)retNode;
}

template<typename DATA>
inline void SMemoryPool<DATA>::Free(DATA* ptr)
{
	Node* newTop = (Node*)ptr;
	Node* nowTop;

	// NOTE
	// _InterlockedCompareExchange128은 _InterlockedCompareExchange64보다 더 느리다.
	// 따라서 Free부분에서는 128 사용하지 않고 64를 채택함
	// 그로인해서 Alloc쪽 Node를 Pop하는 부분에서 문제 발생할 수 있는점 생각할 수 있어야 된다.

	do
	{
		nowTop = _top->nodePtr;
		newTop->next = nowTop;
	} while (_InterlockedCompareExchange64((LONG64*)&_top->nodePtr, (LONG64)newTop, (LONG64)nowTop) != (LONG64)nowTop);

	_InterlockedIncrement(&_nodeCount);
	_InterlockedDecrement(&_useCount);
}