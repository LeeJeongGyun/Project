#pragma once

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <pdh.h>
#include <pdhmsg.h>

#pragma comment(lib, "pdh.lib")

struct ETHERNET
{
	bool bUse;
	WCHAR szName[128];

	PDH_HCOUNTER Network_RecvBytes;
	PDH_HCOUNTER Network_SendBytes;
};

class PDHMonitor
{
public:
	PDHMonitor(const WCHAR* szProcessName);
	~PDHMonitor();

	void UpdatePDHCounter();

	inline double GetNonpagedPoolUsage() { return _nonpagedPoolValue.doubleValue; }
	inline double GetProcessUserMemoryUsage() { return _processUserMemoryValue.doubleValue; }
	inline double GetAvailableMemoryUsage() { return _availableMemoryValue.doubleValue; }
	inline double GetVirtualMemorySize() { return _virtualMemoryValue.doubleValue; }
	inline double GetEthernetRecvBytes() { return _ethernetRecvBytesValue; }
	inline double GetEthernetSendBytes() { return _ethernetSendBytesValue; }

private:
	PDH_HQUERY	 _Query;
	PDH_HCOUNTER _nonpagedPool;
	PDH_HCOUNTER _processUserMemory;
	PDH_HCOUNTER _availableMemory;
	PDH_HCOUNTER _virtualMemory;

	PDH_FMT_COUNTERVALUE _nonpagedPoolValue;
	PDH_FMT_COUNTERVALUE _processUserMemoryValue;
	PDH_FMT_COUNTERVALUE _availableMemoryValue;
	PDH_FMT_COUNTERVALUE _virtualMemoryValue;
	PDH_FMT_COUNTERVALUE _networkValue;

	
	ETHERNET _ethernetStruct[10];
	double _ethernetRecvBytesValue;
	double _ethernetSendBytesValue;
};

