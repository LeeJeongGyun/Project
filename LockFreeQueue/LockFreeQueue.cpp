// LockFreeQueue.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//
#include <iostream>
#include<thread>
#include<vector>
#include<Windows.h>
#include "CrashDump.h"
#include "LockFreeQueue.h"
using namespace std;

#define dfTHREAD_NUM                4
#define dfTOTAL_DATA_SIZE           4
#define dfTHREAD_DATA_SIZE          1

CrashDump dump;
LockFreeQueue<int*> que;

void Crash()
{
    int* ptr = nullptr;
    *ptr = 0xABCD;
}

void Thread(int idx)
{
    int** ptr = new int* [dfTHREAD_DATA_SIZE];

    while (true)
    {
        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            que.Dequeue(ptr[i]);
        }

        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            _InterlockedIncrement((unsigned int*)ptr[i]);
        }

        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            if (*ptr[i] != 1)
            {
                Crash();
            }
        }

        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            _InterlockedDecrement((unsigned int*)ptr[i]);
        }

        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            if (*ptr[i] != 0)
            {
                Crash();
            }
        }

        for (int i = 0; i < dfTHREAD_DATA_SIZE; i++)
        {
            que.Enqueue(ptr[i]);
        }
    }
}

int main()
{
    vector<thread> v;

    for (int i = 0; i < dfTOTAL_DATA_SIZE; i++)
    {
        que.Enqueue(new int(0));
    }

    for (int i = 0; i < dfTHREAD_NUM; i++)
    {
        v.push_back(thread(Thread, i));
    }

    while (true)
    {
        wprintf(L"UseCount: %d\n", que.GetUseCount());
        Sleep(1000);
    }

    for (int i = 0; i < dfTHREAD_NUM; i++)
    {
        v[i].join();
    }
}

