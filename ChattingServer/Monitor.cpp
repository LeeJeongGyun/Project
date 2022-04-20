
#include "Monitor.h"
#include <windows.h>
#include <strsafe.h>
#include "pch.h"

PDHMonitor::PDHMonitor(const WCHAR* szProcessName)
{	
	for (int iCnt = 0; iCnt < 10; iCnt++)
		_ethernetStruct[iCnt].bUse = false;

	PdhOpenQuery(NULL, NULL, &_Query);
	
	WCHAR userMemoryQuery[1024] = { 0, };
	WCHAR virtualMemoryQuery[1024] = { 0, };
	
	StringCbPrintfW(userMemoryQuery, 1024, L"\\Process(%s)\\Private Bytes", szProcessName);
	StringCbPrintfW(virtualMemoryQuery, 1024, L"\\Process(%s)\\Virtual Bytes", szProcessName);

	PdhAddCounter(_Query, L"\\Memory\\Pool Nonpaged Bytes", NULL, &_nonpagedPool);
	PdhAddCounter(_Query, userMemoryQuery, NULL, &_processUserMemory);
	PdhAddCounter(_Query, virtualMemoryQuery, NULL, &_virtualMemory);
	PdhAddCounter(_Query, L"\\Memory\\Available MBytes", NULL, &_availableMemory);

	WCHAR* szCur = NULL;
	WCHAR* szCounter = NULL;
	WCHAR* szInterfaces = NULL;
	WCHAR szQuery[1024] = { 0, };

	DWORD dwCounterSize = 0, dwInterfaceSize = 0;

	PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounter, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0);

	szCounter = new WCHAR[dwCounterSize];
	szInterfaces = new WCHAR[dwInterfaceSize];

	if (ERROR_SUCCESS != PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounter, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0))
	{
		delete[] szCounter;
		delete[] szInterfaces;
		return;
	}

	int iCnt = 0;
	szCur = szInterfaces;

	for (; *szCur != L'\0' && iCnt < 10; szCur += wcslen(szCur) + 1, iCnt++)
	{
		_ethernetStruct[iCnt].bUse = true;
		_ethernetStruct[iCnt].szName[0] = L'\0';

		wcscpy_s(_ethernetStruct[iCnt].szName, 128, szCur);

		szQuery[0] = L'\0';
		StringCbPrintfW(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Received/sec", szCur);
		PdhAddCounter(_Query, szQuery, NULL, &_ethernetStruct[iCnt].Network_RecvBytes);

		szQuery[0] = L'\0';
		StringCbPrintfW(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Sent/sec", szCur);
		PdhAddCounter(_Query, szQuery, NULL, &_ethernetStruct[iCnt].Network_SendBytes);
	}
}

PDHMonitor::~PDHMonitor()
{
}

void PDHMonitor::UpdatePDHCounter()
{
	PdhCollectQueryData(_Query);

	PdhGetFormattedCounterValue(_nonpagedPool, PDH_FMT_DOUBLE, NULL, &_nonpagedPoolValue);
	PdhGetFormattedCounterValue(_processUserMemory, PDH_FMT_DOUBLE, NULL, &_processUserMemoryValue);
	PdhGetFormattedCounterValue(_availableMemory, PDH_FMT_DOUBLE, NULL, &_availableMemoryValue);
	PdhGetFormattedCounterValue(_virtualMemory, PDH_FMT_DOUBLE, NULL, &_virtualMemoryValue);

	_ethernetRecvBytesValue = 0;
	_ethernetSendBytesValue = 0;
	for (int iCnt = 0; iCnt < 10; iCnt++)
	{
		if (_ethernetStruct[iCnt].bUse)
		{
			bool status;
			status = PdhGetFormattedCounterValue(_ethernetStruct[iCnt].Network_RecvBytes, PDH_FMT_DOUBLE, NULL, &_networkValue);
			if (!status) _ethernetRecvBytesValue += _networkValue.doubleValue;

			status = PdhGetFormattedCounterValue(_ethernetStruct[iCnt].Network_SendBytes, PDH_FMT_DOUBLE, NULL, &_networkValue);
			if (!status) _ethernetSendBytesValue += _networkValue.doubleValue;
		}
	}
}