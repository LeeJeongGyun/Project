#pragma once

#include<Windows.h>
#include "SMemoryPool.h"
#define dfLOG_ARR_SIZE 16384

template<typename DATA>
class LockFreeQueue
{
	struct Node;

	struct alignas(64) CountedNode
	{
		LONG64 uniqueCount;
		Node* nodePtr;
	};


	struct Node
	{
		DATA data;
		Node* next;
	};

	// LOG
	struct Log
	{
		DWORD id;
		char type;
		Node* head;
		Node* nextHead;
		Node* copyTail;
		Node* nowTail;
		Node* newTail;
		DATA dataPtr;
	};

public:
	LockFreeQueue();
	~LockFreeQueue();

	void Enqueue(const DATA& data);
	bool Dequeue(DATA& data);

	long GetUseCount() { return _useCount; }

	inline void PrintLog(BYTE type, Node* head, Node* nextHead, Node* copytail, Node* nowTail, Node* newTail, DATA ptr = nullptr);

private:
	CountedNode* _head;
	CountedNode* _tail;
	SMemoryPool<Node> _pool;

	alignas(64) long _useCount;

	// LOG
	Log _memoryLog[dfLOG_ARR_SIZE];
	LONG64 _logIndex = -1;
};

template<typename DATA>
inline LockFreeQueue<DATA>::LockFreeQueue()
	:_useCount(0), _logIndex(-1)
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

	//LOG
	Node* head;

	while (true)
	{
		tail.uniqueCount = _tail->uniqueCount;
		tail.nodePtr = _tail->nodePtr;
		next = tail.nodePtr->next;

		// LOG
		head = _head->nodePtr;
		PrintLog(1, head, head->next, tail.nodePtr, _tail->nodePtr, pNewNode, pNewNode->data);

		if (next == nullptr)
		{
			if (_InterlockedCompareExchangePointer((PVOID*)&tail.nodePtr->next, pNewNode, nullptr) == nullptr)
			{
				//LOG
				PrintLog(2, head, head->next, tail.nodePtr, _tail->nodePtr, pNewNode, pNewNode->data);

				// NOTE
				// 다른 곳에서 tail을 이동시키면서 Enq, Deq가 발생되기 때문에 
				// pNewNode가 이미 release될 수 있는 가능성이 존재한다.
				// 따라서 여기서의 DoubleCAS는 무조건 써야된다.
				_InterlockedCompareExchange128((LONG64*)_tail, (LONG64)pNewNode, tail.uniqueCount + 1, (LONG64*)&tail);
				break;
			}
		}
		else
		{
			// NOTE
			// 위에와 마찬가지의 이유로 DoubleCAS 사용해야 된다.
			// 현재 _tail->next가 NULL이 아니면 내가 옮겨놓고 Enq하고 나가야지.
			_InterlockedCompareExchange128((LONG64*)_tail, (LONG64)next, tail.uniqueCount + 1, (LONG64*)&tail);
		}
	}

	//LOG
	PrintLog(3, head, head->next, tail.nodePtr, _tail->nodePtr, pNewNode, pNewNode->data);

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
					_InterlockedCompareExchange128((LONG64*)_tail, (LONG64)next, tail.uniqueCount + 1, (LONG64*)&tail);
				}
			}
			else
			{
				copyData = nextHead->data;

				if (_InterlockedCompareExchange128((LONG64*)_head, (LONG64)nextHead, releaseHead.uniqueCount + 1, (LONG64*)&releaseHead))
					break;
			}
		}
	}

	//LOG
	PrintLog(4, releaseHead.nodePtr, nextHead, tail.nodePtr, _tail->nodePtr, tail.nodePtr->next, copyData);

	data = copyData;
	_pool.Free(releaseHead.nodePtr);
	
	return true;
}

template<typename DATA>
inline void LockFreeQueue<DATA>::PrintLog(BYTE type, Node* head, Node* nextHead, Node* copytail, Node* nowTail, Node* newTail, DATA dataPtr)
{
	LONG64 localIndex = _InterlockedIncrement64(&_logIndex);
	localIndex &= (dfLOG_ARR_SIZE - 1);

	_memoryLog[localIndex].id = GetCurrentThreadId();
	_memoryLog[localIndex].type = type;
	_memoryLog[localIndex].head = head;
	_memoryLog[localIndex].nextHead = nextHead;
	_memoryLog[localIndex].copyTail = copytail;
	_memoryLog[localIndex].nowTail = nowTail;
	_memoryLog[localIndex].newTail = newTail;
	_memoryLog[localIndex].dataPtr = dataPtr;
}
