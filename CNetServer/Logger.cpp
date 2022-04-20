#include "pch.h"
#include "Logger.h"

Logger::Logger(BYTE logLevel, const WCHAR* pDirectory)
	:_dwCount(0), _logLevel(logLevel)
{
	InitializeSRWLock(&_lock);
	_szLogLevel[0] = const_cast<WCHAR*>(L"DEBUG");
	_szLogLevel[1] = const_cast<WCHAR*>(L"SYSTEM");
	_szLogLevel[2] = const_cast<WCHAR*>(L"ERROR");

	wmemcpy(_szFolder, pDirectory, 32);
	_wmkdir(pDirectory);
}

void Logger::Log(const WCHAR* pType, enLOG_LEVEL logLevel, const WCHAR* pString, ...)
{
	if (_logLevel > logLevel)
		return;

	FILE* pFile = nullptr;
	errno_t err;
	HRESULT hResult;
	WCHAR szFileName[128];
	WCHAR szMessage[1024];
	WCHAR szVMessage[1024];
	SYSTEMTIME TIME;

	GetLocalTime(&TIME);
	StringCchPrintfW(szFileName, 128, L"%s\\%d%02d_%s.txt", _szFolder, TIME.wYear, TIME.wMonth, pType);

	AcquireSRWLockExclusive(&_lock);
	err = _wfopen_s(&pFile, szFileName, L"at");
	if (err != 0 || pFile == nullptr)
	{
		CrashDump::Crash();
		return;
	}

	_dwCount++;
	StringCchPrintfW(szMessage, 1024, L"[%s] [%d-%02d-%02d %d:%d:%d / %s / %09d] ",
		pType, TIME.wYear, TIME.wMonth, TIME.wDay, TIME.wHour, TIME.wMinute, TIME.wSecond, _szLogLevel[logLevel], _dwCount);

	va_list va;
	va_start(va, pString);
	hResult = StringCchVPrintfW(szVMessage, 1024, pString, va);
	va_end(va);

	if (STRSAFE_E_INSUFFICIENT_BUFFER == StringCchCatW(szMessage, 1024, szVMessage))
	{
		CrashDump::Crash();
		return;
	}

	if (fwprintf_s(pFile, szMessage) < 0)
	{
		CrashDump::Crash();
		return;
	}

	fclose(pFile);
	ReleaseSRWLockExclusive(&_lock);
}

void Logger::LogHex(const WCHAR* pType, enLOG_LEVEL logLevel, const WCHAR* pLog, BYTE* pByte, int byteLen)
{
	if (_logLevel > logLevel)
		return;

	FILE* pFile = nullptr;
	errno_t err;
	WCHAR szFileName[128];
	WCHAR szMessage[1024];
	SYSTEMTIME TIME;

	GetLocalTime(&TIME);
	StringCchPrintfW(szFileName, 128, L"%s\\%d%02d_%s_HEX.txt", _szFolder, TIME.wYear, TIME.wMonth, pType);

	AcquireSRWLockExclusive(&_lock);
	err = _wfopen_s(&pFile, szFileName, L"at");
	if (err != 0 || pFile == nullptr)
	{
		CrashDump::Crash();
		return;
	}

	_dwCount++;
	StringCchPrintfW(szMessage, 1024, L"[%s] [%d-%02d-%02d %d:%d:%d / %s / %09d] %s\n",
		pType, TIME.wYear, TIME.wMonth, TIME.wDay, TIME.wHour, TIME.wMinute, TIME.wSecond, _szLogLevel[logLevel], _dwCount, pLog);

	if (0 > fwprintf_s(pFile, szMessage))
	{
		CrashDump::Crash();
		return;
	}

	for (int iCnt = 0; iCnt < byteLen; iCnt++)
	{
		if (0 > fwprintf(pFile, L"%x ", pByte[iCnt]))
		{
			CrashDump::Crash();
		}
	}
	fwprintf(pFile, L"\n");

	fclose(pFile);
	ReleaseSRWLockExclusive(&_lock);
}
