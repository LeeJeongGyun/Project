#include "pch.h"
#include "Global.h"

long CrashDump::_DumpCount = 0;
uint8 Packet::_packetKey = 0;
uint8 Packet::_packetCode = 0;
CrashDump g_Dump;

#ifndef _PERFORMANCE
	ulong64 g_ChunkGuard = 0xDDDDDDDDDDDDDDDD;
	ulong64 g_Guard = 0xAAAAAAAAAAAAAAAA;
#endif