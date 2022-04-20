#pragma once

#include<Windows.h>
#include "MemoryPool.h"
#include "Macro.h"

template<typename DATA>
class LockFreeQueue
{
	struct Node;

	struct alignas(16) CountedNode
	{
		long64 uniqueCount;
		Node* nodePtr;
	};

	struct Node
	{
		inline void PlacementNewInit() {}
		inline void Init() {}
		DATA data;
		Node* next;
	};

public:
	LockFreeQueue();
	~LockFreeQueue();

	void Enqueue(const DATA& data);
	bool Dequeue(DATA& data);
	void ClearBuffer();

	long GetUseCount() { return _useCount; }
private:
	CountedNode* _head;
	CountedNode* _tail;
	MemoryPool<Node> _pool;

	alignas(64) long _useCount;
};

template<typename DATA>
inline LockFreeQueue<DATA>::LockFreeQueue()
	:_useCount(0)
{
	// NOTE
	// CAS연산을 할 때 ENQ와 DEQ는 전혀 연관이 없음으로 캐시라인만큼 떨어뜨려놔서
	// ENQ CAS연산을 할 때 DEQ CAS연산이 영향 받지 않도록 하기 위해 64BYTE 경계에 맞춘다.
	_head = (CountedNode*)_aligned_malloc(sizeof(CountedNode), 64);
	_tail = (CountedNode*)_aligned_malloc(sizeof(CountedNode), 64);
	_head->nodePtr = _pool.Alloc();
	_tail->nodePtr = _head->nodePtr;
	_head->uniqueCount = 0;
	_tail->uniqueCount = 0;
	_tail->nodePtr->next = nullptr;
}

template<typename DATA>
inline LockFreeQueue<DATA>::~LockFreeQueue()
{
	Node* node = _head->nodePtr;
	_aligned_free(_head);
	_aligned_free(_tail);

	while (node)
	{
		Node* dNode = node;
		node = node->next;
		_pool.Free(dNode);
	}
}

template<typename DATA>
inline void LockFreeQueue<DATA>::Enqueue(const DATA& data)
{
	CountedNode tail;
	Node* next;

	// NOTE
	// ENQ에 들어올 때만 Alloc받은 노드의 next를 NULL로 밀어야 된다.
	// 과거의 tail 포인터를 가지고 CAS연산을 하기 때문에 tail 포인터가 가리키는 공간이 어디인지 예측 불가능하다.
	// 따라서 아무노드나 next가 NULL이 될 경우 Queue가 아닌 공간에 새로운 노드를 연결할 가능성이 생긴다.
	// 따라서 ENQ에서만 tail의 next를 NULL로 밀자!
	Node* pNewNode = _pool.Alloc();
	pNewNode->next = nullptr;
	pNewNode->data = data;

	while (true)
	{
		tail.uniqueCount = _tail->uniqueCount;
		tail.nodePtr = _tail->nodePtr;
		next = tail.nodePtr->next;

		if (next == nullptr)
		{
			if (_InterlockedCompareExchangePointer((PVOID*)&tail.nodePtr->next, pNewNode, nullptr) == nullptr)
			{
				// NOTE
				// 다른 곳에서 tail을 이동시키면서 Enq, Deq가 발생되기 때문에 
				// pNewNode가 이미 release될 수 있는 가능성이 존재한다.
				// 따라서 여기서의 DoubleCAS는 무조건 써야된다.
				_InterlockedCompareExchange128((long64*)_tail, (long64)pNewNode, tail.uniqueCount + 1, (long64*)&tail);
				break;
			}
		}
		else
		{
			// NOTE
			// 위에와 마찬가지의 이유로 DoubleCAS 사용해야 된다.
			// 현재 _tail->next가 NULL이 아니면 내가 옮겨놓고 Enq하고 나가야지.
			_InterlockedCompareExchange128((long64*)_tail, (long64)next, tail.uniqueCount + 1, (long64*)&tail);
		}
	}

	_InterlockedIncrement(&_useCount);
}

template<typename DATA>
inline bool LockFreeQueue<DATA>::Dequeue(DATA& data)
{
	if (_InterlockedDecrement(&_useCount) < 0)
	{
		_InterlockedIncrement(&_useCount);
		return false;
	}

	CountedNode releaseHead;
	Node* nextHead;
	DATA copyData = 0;

	CountedNode tail;
	Node* next;

	while (true)
	{
		releaseHead.uniqueCount = _head->uniqueCount;
		releaseHead.nodePtr = _head->nodePtr;
		nextHead = releaseHead.nodePtr->next;

		if (nextHead != NULL)
		{
			tail.uniqueCount = _tail->uniqueCount;
			tail.nodePtr = _tail->nodePtr;

			// NOTE 
			// _head가 _tail 넘어가는 경우 방지
			if (releaseHead.nodePtr == tail.nodePtr)
			{
				// NOTE
				// _head가 _tail 넘어가지 않으면 발생할 수 있는 Deque 무한루프 방지
				next = tail.nodePtr->next;

				if (next)
				{
					_InterlockedCompareExchange128((long64*)_tail, (long64)next, tail.uniqueCount + 1, (long64*)&tail);
				}
			}
			else
			{
				copyData = nextHead->data;

#ifndef dfSMART_PACKET_PTR
				if (_InterlockedCompareExchange128((long64*)_head, (long64)nextHead, releaseHead.uniqueCount + 1, (long64*)&releaseHead))
					break;
#else
				if (_InterlockedCompareExchange128((long64*)_head, (long64)nextHead, releaseHead.uniqueCount + 1, (long64*)&releaseHead))
					break;
				else
				{
					// TODO
					// 이유 찾자 
					//DATA(data._ptr).~DATA();
					copyData .~DATA();
				}
#endif
				
			}
		}
	}

	data = copyData;
	_pool.Free(releaseHead.nodePtr);

#ifdef dfSMART_PACKET_PTR
	copyData .~DATA();
#endif

	return true;
}

template<typename DATA>
inline void LockFreeQueue<DATA>::ClearBuffer()
{
	DATA data;
	while (Dequeue(data));
}
