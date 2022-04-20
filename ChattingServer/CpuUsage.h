#pragma once

#include<Windows.h>

class CpuUsage
{
public:
	CpuUsage(HANDLE hProcess = INVALID_HANDLE_VALUE);

	void UpdateCpuTime(void);

	float GetProcessorTotal(void) { return _processorTotal; }
	float GetProcessorUser(void) { return _processorUser; }
	float GetProcessorKernel(void) { return _processorKernel; }

	float GetProcessTotal(void) { return _processTotal; }
	float GetProcessUser(void) { return _processUser; }
	float GetProcessKernel(void) { return _processKernel; }

private:
	HANDLE	_hProcess;
	int		_numOfProcessor;

	float _processorTotal;
	float _processorUser;
	float _processorKernel;
							
	float _processTotal;
	float _processUser;
	float _processKernel;

	ULARGE_INTEGER _processor_LastKernel;
	ULARGE_INTEGER _processor_LastUser;
	ULARGE_INTEGER _processor_LastIdle;

	ULARGE_INTEGER _process_LastKernel;
	ULARGE_INTEGER _process_LastUser;
	ULARGE_INTEGER _process_LastTime;
};