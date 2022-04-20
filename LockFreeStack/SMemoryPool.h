#pragma once

#include<Windows.h>

template<typename DATA>
class SMemoryPool
{
	struct Node;

	struct alignas(16) CountedNode
	{
		LONG64 uniqueCount;
		Node* nodePtr;
	};

	struct Node
	{
		DATA data;
		Node* next;
	};

public:
	SMemoryPool(bool bPlacementNew = false);
	~SMemoryPool();

	DATA*			Alloc();
	void			Free(DATA* ptr);

	unsigned int	GetAllocCount() { return _allocCount; }
	long			GetUseCount() { return _allocCount - _nodeCount; }
	long			GetNodeCount() { return _nodeCount; }

private:
	bool				_bPlacementNew;
	CountedNode*		_top;

	alignas(64) unsigned int		_allocCount;
	alignas(64) long				_nodeCount;
};

template<typename DATA>
inline SMemoryPool<DATA>::SMemoryPool(bool bPlacementNew)
	:_allocCount(0), _nodeCount(0), _bPlacementNew(bPlacementNew)
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

	if (_nodeCount > 0)
	{
		if ( InterlockedDecrement(&_nodeCount) >= 0)
		{
			do
			{
				popTop.uniqueCount = _top->uniqueCount;
				popTop.nodePtr = _top->nodePtr;
			} while (false == _InterlockedCompareExchange128((LONG64*)_top, (LONG64)popTop.nodePtr->next, popTop.uniqueCount + 1, (LONG64*)&popTop));

			retNode = popTop.nodePtr;
		}
		else
		{
			InterlockedIncrement(&_nodeCount);
		}
	}

	if (retNode == nullptr)
	{
		if (_bPlacementNew)
		{
			retNode = (Node*)malloc(sizeof(Node));
			new (retNode) DATA;
		}
		else
		{
			retNode = new Node;
		}
		
		InterlockedIncrement(&_allocCount);
	}

	return (DATA*)retNode;
}

template<typename DATA>
inline void SMemoryPool<DATA>::Free(DATA* ptr)
{
	if (_bPlacementNew)
	{
		ptr->~DATA();
	}
	Node* newTop = (Node*)ptr;
	Node* nowTop;
	
	do
	{
		nowTop = _top->nodePtr;
		newTop->next = nowTop;
	} while (_InterlockedCompareExchange64((LONG64*)&_top->nodePtr, (LONG64)newTop, (LONG64)nowTop) != (LONG64)nowTop);

	InterlockedIncrement(&_nodeCount);
}