#pragma once

#include "pch.h"

enum enLOG_LEVEL : BYTE
{
	eLOG_DEBUG = 0,
	eLOG_SYSTEM = 1,
	eLOG_ERROR = 2,
};

class Logger
{
private:
	Logger(BYTE logLevel, const WCHAR* pDirectory);
	
public:
	void Log(const WCHAR* pType, enLOG_LEVEL logLevel, const WCHAR* pString, ...);
	void LogHex(const WCHAR* pType, enLOG_LEVEL logLevel, const WCHAR* pLog, BYTE* pByte, int byteLen);

	static Logger* GetInstance()
	{
		static Logger logger(eLOG_SYSTEM, L"ChattingServerLog");
		return &logger;
	}

private:
	DWORD	_dwCount = 0;
	BYTE	_logLevel = 0;
	WCHAR	_szFolder[32];
	WCHAR* _szLogLevel[3];
	SRWLOCK	_lock;
};
