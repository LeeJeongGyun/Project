#pragma once

#include "pch.h"
#include "Macro.h"

#ifndef _PERFORMANCE
	extern ulong64 g_ChunkGuard;
	extern ulong64 g_Guard;
#endif

//enum eTYPE
//{
//	RECV = 0,
//	SEND = 1
//};
//
//// SessionId 상위 2바이트(세션 배열의 인덱스), 하위 6바이트(세션의 Unique Id)
//// 나눠서 사용함으로써 빠르게 세션배열을 검색
//typedef union
//{
//	struct
//	{
//		uint64 id : 48;
//		uint64 index : 16;
//	}DUMMYSTRUCTNAME;
//
//	uint64 sessionId;
//}SessionId;