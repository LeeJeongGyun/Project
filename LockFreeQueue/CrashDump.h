#pragma once

#include<Windows.h>
#include<stdio.h>
#include<Psapi.h>
#include<DbgHelp.h>
#include<crtdbg.h>

#pragma comment(lib, "Dbghelp.lib")

class CrashDump
{
public:
	static long _DumpCount;

	CrashDump()
	{
		_DumpCount = 0;
		
		_invalid_parameter_handler oldHandler, newHandler;
		newHandler = myInvalidParameterHandler;

		oldHandler = _set_invalid_parameter_handler(newHandler);
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);

		_CrtSetReportHook(_custom_Report_hook);

		_set_purecall_handler(MyPurecallHandler);

		SetHandlerDump();
	}

	static void Crash(void)
	{
		int* p = nullptr;
		*p = 0;
	}

	static LONG WINAPI MyExceptionFilter(PEXCEPTION_POINTERS pExceptionPointer)
	{
		int iWorkingMemory = 0;
		SYSTEMTIME Time;

		long DumpCount = InterlockedIncrement(&_DumpCount);

		HANDLE hProcess = 0;
		PROCESS_MEMORY_COUNTERS pmc;

		hProcess = GetCurrentProcess();

		if (NULL == hProcess) return 0;

		if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
		{
			iWorkingMemory = (int)(pmc.WorkingSetSize / 1024 / 1024);
		}
		CloseHandle(hProcess);

		WCHAR fileName[MAX_PATH];
		GetLocalTime(&Time);
		wsprintfW(fileName, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d_%dMB.dmp",
			Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, DumpCount, iWorkingMemory);

		wprintf(L"\n\n\n!!! Crash Error !!! %d.%d.%d / %d:%d:%d\n",
			Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);

		wprintf(L"Now Save dump file....\n");

		HANDLE hDumpFile = ::CreateFile(fileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION miniDumpExceptionInformation;

			miniDumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
			miniDumpExceptionInformation.ExceptionPointers = pExceptionPointer;
			miniDumpExceptionInformation.ClientPointers = TRUE;

			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &miniDumpExceptionInformation, NULL, NULL);
			CloseHandle(hDumpFile);

			wprintf(L"CrashDump Save Finish !\n");
		}

		return EXCEPTION_EXECUTE_HANDLER;

	}

	static void SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
	{
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char* message, int* returnValue)
	{
		Crash();
		return true;
	}

	static void MyPurecallHandler(void)
	{
		Crash();
	}
};

long CrashDump::_DumpCount = 0;