#pragma once

#include<windows.h>
#include<stdio.h>
#define dfPROFILE_LEN				100
#define dfTHREAD_NUM				50
#define dfFUNCTION_NAME_LEN			128

#define USE_PROFILER(szFunc)			WrapProfiler profile(szFunc)

#include<iostream>

class Profiler
{
	typedef struct st_PROFILE
	{
		st_PROFILE()
		{
			bFlag = FALSE;
			iTotalTime = 0;
			iMin = INT_MAX;
			iMax = 0;
			iCall = 0;
			lStartTime.QuadPart = 0;
			dwThreadId = GetCurrentThreadId();
		}

		BOOL			bFlag;
		WCHAR			szName[dfFUNCTION_NAME_LEN];
		DWORD			dwThreadId;

		// 위 3개의 멤버변수는 한번 저장하고 계속 load하는 구조
		// 밑에 멤버변수는 계속 store, load하는 구조이기 때문에
		// 캐시미스율 낮추기 위해서 alignas로 떼어놓는 방법 선택했다.
		alignas(64) LARGE_INTEGER	lStartTime;
		LONG64						iTotalTime;
		LONG64						iMin;
		LONG64						iMax;
		LONG64						iCall;
	}st_PROFILE;

public:
	inline Profiler();
	inline ~Profiler();

	inline void ProfileBegin(const WCHAR* funcName);
	inline void ProfileEnd(const WCHAR* funcName);
	inline void ProfileDataOutText(const WCHAR* szFileName);
	inline void ProfileReset(void);
	inline st_PROFILE* GetProfilerPtr(void);

private:
	DWORD			_dwTlsIndex;

	st_PROFILE*		_threadProfilerArr[dfTHREAD_NUM];
	SHORT			_threadIndex;
};

Profiler::Profiler()
	:_threadIndex(-1)
{
	_dwTlsIndex = TlsAlloc();
	if (_dwTlsIndex == TLS_OUT_OF_INDEXES)
	{
		// TODO
		// LOG 남겨야지 => 상세히 ex) Profiler에서 TLS 사용하려다가 문제 발생했다는 걸 알 수 있게끔
		// CRASH
	}
}

Profiler::~Profiler()
{
	TlsFree(_dwTlsIndex);
}

void Profiler::ProfileBegin(const WCHAR* szFuncName)
{
	st_PROFILE* pProfiler = GetProfilerPtr();

	int iCnt;
	for (iCnt = 0; iCnt < dfPROFILE_LEN; iCnt++)
	{
		if (pProfiler[iCnt].bFlag == TRUE)
		{
			if (wcscmp(pProfiler[iCnt].szName, szFuncName) == 0)
				break;
		}
		else
		{
			pProfiler[iCnt].bFlag = TRUE;
			wcscpy_s(pProfiler[iCnt].szName, dfFUNCTION_NAME_LEN, szFuncName);
			break;
		}
	}

	if (pProfiler[iCnt].lStartTime.QuadPart != 0)
	{
		// TODO
		// LOG
		// CRASH
		wprintf(L"Don`t use ProEnd\n");
		exit(1);
	}

	pProfiler[iCnt].iCall++;
	QueryPerformanceCounter(&pProfiler[iCnt].lStartTime);
}

void Profiler::ProfileEnd(const WCHAR* szFuncName)
{
	LARGE_INTEGER lEndTime;
	QueryPerformanceCounter(&lEndTime);

	st_PROFILE* pProfiler = GetProfilerPtr();

	int iCnt;
	for (iCnt = 0; iCnt < dfPROFILE_LEN; iCnt++)
	{
		if (0 == wcscmp(pProfiler[iCnt].szName, szFuncName))
			break;
	}

	// ASSEMBLY 명령어 줄이기 위한 지역변수 복사
	LONG64 startTime = pProfiler[iCnt].lStartTime.QuadPart;
	LONG64 diffTime;

	if (iCnt >= dfPROFILE_LEN)
	{
		// TODO
		// LOG
		// CRASH
		wprintf(L"Can`t look for Function Name");
		exit(1);
	}

	if (startTime == 0)
	{
		// TODO
		// LOG
		// CRASH
		wprintf(L"Don`t use ProBegin\n");
		exit(1);
	}

	diffTime = lEndTime.QuadPart - startTime;
	pProfiler[iCnt].iTotalTime += diffTime;


	if (diffTime > pProfiler[iCnt].iMax)
		pProfiler[iCnt].iMax = diffTime;

	if (diffTime < pProfiler[iCnt].iMin)
		pProfiler[iCnt].iMin = diffTime;

	// End 안하고 Begin 들어갔을 때 Error 처리
	pProfiler[iCnt].lStartTime.QuadPart = 0;
}

void Profiler::ProfileDataOutText(const WCHAR* szFileName)
{
	int iCnt;
	FILE* pLogFile;
	WCHAR szFile[128];
	wsprintfW(szFile, L"%s.txt", szFileName);

	errno_t err = _wfopen_s(&pLogFile, szFile, L"wt,ccs=UTF-16LE");
	if (err != 0)
	{
		// TODO
		// LOG
		// CRASH
		wprintf(L"File Open Fail!!\n");
		exit(1);
	}

	if (pLogFile == nullptr)
	{
		// TODO
		// LOG
		// CRASH
		exit(1);
	}

	// TOTAL을 구하기 위해 선언
	st_PROFILE* pTotalProfiler = (st_PROFILE*)_aligned_malloc(sizeof(st_PROFILE) * dfPROFILE_LEN, 64); 
	memset(pTotalProfiler, 0, sizeof(st_PROFILE) * dfPROFILE_LEN);

	st_PROFILE* pProfiler = nullptr;
	LONG64 threadIndex = _threadIndex;

	LONG64 totalTime;
	LONG64 maxTime;
	LONG64 minTime;
	LONG64 callCnt;
	LONG64 threadCnt;

	for (int idx = 0; idx <= threadIndex; idx++)
	{
		pProfiler = _threadProfilerArr[idx];

		fwprintf_s(pLogFile, L"----------------------------------------------------------------------------------------------\n\n");
		fwprintf_s(pLogFile, L"%15s  |%15s  |%12s  |%11s   |%11s   |%10s |\n", L"ThreadId", L"Name", L"Average", L"Min", L"Max", L"Call");
		fwprintf_s(pLogFile, L"----------------------------------------------------------------------------------------------\n\n");


		for (iCnt = 0; iCnt < dfPROFILE_LEN; ++iCnt)
		{
			if (pProfiler[iCnt].bFlag == FALSE)
				break;

			totalTime = pProfiler[iCnt].iTotalTime;
			maxTime = pProfiler[iCnt].iMax;
			minTime = pProfiler[iCnt].iMin;
			callCnt = pProfiler[iCnt].iCall;

			fwprintf_s(pLogFile, L"%16d |%16s |%11.4fµs |%11.4fµs |%11.4fµs |%10lld  |\n",
				pProfiler[iCnt].dwThreadId, pProfiler[iCnt].szName,
				(double)(totalTime - maxTime - minTime) / (double)(callCnt - 2) / 10.,
				minTime / 10.,
				maxTime / 10.,
				callCnt
			);

			pTotalProfiler[iCnt].iTotalTime += totalTime;
			pTotalProfiler[iCnt].iMin += minTime;
			pTotalProfiler[iCnt].iMax += maxTime;
			pTotalProfiler[iCnt].iCall += callCnt;
		}

		fwprintf_s(pLogFile, L"\n----------------------------------------------------------------------------------------------\n\n");
	}

	fwprintf_s(pLogFile, L"----------------------------------------------------------------------------------------------\n\n");
	fwprintf_s(pLogFile, L"%15s  |%15s  |%12s  |%11s   |%11s   |%10s |\n", L"ThreadId", L"Name", L"Average", L"Min", L"Max", L"Call");
	fwprintf_s(pLogFile, L"----------------------------------------------------------------------------------------------\n\n");

	for (int i = 0; i < dfPROFILE_LEN; ++i)
	{
		if (pTotalProfiler[i].iCall == 0)
			break;

		totalTime = pTotalProfiler[i].iTotalTime;
		maxTime = pTotalProfiler[i].iMax;
		minTime = pTotalProfiler[i].iMin;
		callCnt = pTotalProfiler[i].iCall;
		threadCnt = threadIndex + 1;

		fwprintf_s(pLogFile, L"%16s |%16s |%11.4fµs |%11.4fµs |%11.4fµs |%10lld  |\n",
			L"TOTAL", pProfiler[i].szName,
			(double)(totalTime - maxTime - minTime) / (double)(callCnt - 2 * threadCnt) / 10.,
			(double)minTime / threadCnt / 10.,
			(double)maxTime / threadCnt / 10.,
			callCnt
		);
	}

	fwprintf_s(pLogFile, L"\n----------------------------------------------------------------------------------------------\n\n");

	fclose(pLogFile);
}

void Profiler::ProfileReset(void)
{
	int threadIndex = _threadIndex;
	for (int idx = 0; idx <= threadIndex; idx++)
	{
		st_PROFILE* pProfiler = _threadProfilerArr[idx];
		for (int iCnt = 0; iCnt < dfPROFILE_LEN; iCnt++)
		{
			if (pProfiler[iCnt].bFlag == FALSE)
				break;

			pProfiler[iCnt].iCall = 0;
			pProfiler[iCnt].iMax = 0;
			pProfiler[iCnt].iMin = INT_MAX;
			pProfiler[iCnt].iTotalTime = 0;
		}
	}
}

Profiler::st_PROFILE* Profiler::GetProfilerPtr()
{
	st_PROFILE* pProfiler;
	
	// ASSEMBLY 명령어 줄이기 위한 지역변수 복사 
	DWORD dwTlsIndex = _dwTlsIndex;

	if (TlsGetValue(dwTlsIndex) == 0)
	{
		pProfiler = (st_PROFILE*)_aligned_malloc(sizeof(st_PROFILE) * dfPROFILE_LEN, 64);
		
		for (int iCnt = 0; iCnt < dfPROFILE_LEN; iCnt++)
			new (&pProfiler[iCnt]) st_PROFILE;
		
		TlsSetValue(dwTlsIndex, pProfiler);

		_threadProfilerArr[_InterlockedIncrement16(&_threadIndex)] = pProfiler;
	}
	else
	{
		pProfiler = (st_PROFILE*)TlsGetValue(dwTlsIndex);
	}

	return pProfiler;
}

extern Profiler g_Profiler;

class WrapProfiler
{
public:
	WrapProfiler(const WCHAR* pFuncName)
		:_pFuncName(pFuncName)
	{
		g_Profiler.ProfileBegin(pFuncName);
	}
	~WrapProfiler()
	{
		g_Profiler.ProfileEnd(_pFuncName);
	}

private:
	const WCHAR* _pFuncName;
};