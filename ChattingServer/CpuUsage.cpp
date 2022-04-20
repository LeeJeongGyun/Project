
#include <windows.h>
#include "CpuUsage.h"
#include "pch.h"

CpuUsage::CpuUsage(HANDLE hProcess)
{
	if (hProcess == INVALID_HANDLE_VALUE)
		_hProcess = GetCurrentProcess();
	else
		_hProcess = hProcess;


	SYSTEM_INFO sysInfo;

	GetSystemInfo(&sysInfo);
	_numOfProcessor = sysInfo.dwNumberOfProcessors;

	_processorTotal = 0 ;
	_processorUser = 0 ;
	_processorKernel = 0 ;

	_processTotal = 0 ;
	_processUser = 0 ;
	_processKernel = 0 ;

	_processor_LastKernel.QuadPart = 0;
	_processor_LastUser.QuadPart = 0;
	_processor_LastIdle.QuadPart = 0;

	_process_LastKernel.QuadPart = 0;
	_process_LastUser.QuadPart = 0;
	_process_LastTime.QuadPart = 0;

	UpdateCpuTime();
}

void CpuUsage::UpdateCpuTime(void)
{
	// 프로세서 사용률 갱신
	ULARGE_INTEGER Idle;
	ULARGE_INTEGER Kernel;
	ULARGE_INTEGER User;

	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
		return;

	ULONG64 KernelDiff = Kernel.QuadPart - _processor_LastKernel.QuadPart;
	ULONG64 UserDiff = User.QuadPart - _processor_LastUser.QuadPart;
	ULONG64 IdleDiff = Idle.QuadPart - _processor_LastIdle.QuadPart;

	ULONG64 Total = KernelDiff + UserDiff; 
	ULONG64 TimeDiff;

	if (Total == 0)
	{
		_processorTotal = 0.0f;
		_processorKernel = 0.0f;
		_processorUser = 0.0f;
	}
	else
	{
		_processorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		_processorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);
		_processorUser = (float)((double)UserDiff / Total * 100.0f);
	}

	_processor_LastKernel = Kernel;
	_processor_LastUser = User;
	_processor_LastIdle = Idle;

	// 프로세스 사용률 갱신
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;

	GetSystemTimeAsFileTime((LPFILETIME)&NowTime);

	GetProcessTimes(_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);

	TimeDiff = NowTime.QuadPart - _process_LastTime.QuadPart;
	UserDiff = User.QuadPart - _process_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - _process_LastKernel.QuadPart;

	Total = KernelDiff + UserDiff;

	_processTotal = (float)(Total / (double)_numOfProcessor / (double)TimeDiff * 100.0f);
	_processKernel = (float)(KernelDiff / (double)_numOfProcessor / (double)TimeDiff * 100.0f);
	_processUser = (float)(UserDiff / (double)_numOfProcessor / (double)TimeDiff * 100.0f);
	
	_process_LastTime = NowTime;
	_process_LastKernel = Kernel;
	_process_LastUser = User;
}
