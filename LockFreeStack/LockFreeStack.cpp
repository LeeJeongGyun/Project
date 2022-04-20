
#include <iostream>
#include<thread>
#include<vector>
#include "LockFreeStack.h"
#include<windows.h>
#include<conio.h>
using namespace std;

LockFreeStack<int*> s;

#define dfTHREAD_DATA_NUM	10'000
#define dfTHREAD_NUM		10

void Crash(void)
{
	int* ptr = nullptr;
	*ptr = 0xABCDEFFF;
}

void Thread()
{
	int** pArr = new int* [dfTHREAD_DATA_NUM];
	memset(pArr, 0, sizeof(int*) * dfTHREAD_DATA_NUM);
	while (true)
	{
		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			if (s.Pop(pArr[i]) == false)
				Crash();
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			if (*pArr[i] != 0)
				Crash();
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			InterlockedIncrement((unsigned int*)pArr[i]);
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			if (*pArr[i] != 1)
				Crash();	
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			InterlockedDecrement((unsigned int*)pArr[i]);
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			if (*pArr[i] != 0)
				Crash();
		}

		for (int i = 0; i < dfTHREAD_DATA_NUM; i++)
		{
			s.Push(pArr[i]);
		}
	}
}

int main()
{
	for (int i = 0; i < dfTHREAD_NUM * dfTHREAD_DATA_NUM; i++)
	{
		s.Push(new int(0));
	}

	vector<thread> v;

	for (int i = 0; i < dfTHREAD_NUM; i++)
	{
		v.push_back(thread(Thread));
	}
	
	while (true)
	{
		if (_kbhit())
		{
			char key = _getch();
			if (key == VK_RETURN)
				break;
		}
	}


	for (int i = 0; i < dfTHREAD_NUM; i++)
	{
		v[i].join();
	}
}