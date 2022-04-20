#pragma once
#include<Windows.h>
class Parser
{
public:
	Parser();
	~Parser();

	bool LoadFile(const WCHAR* szFileName);

	bool GetValue(const WCHAR* szName, int* ipValue);
	bool GetString(const WCHAR* szName, WCHAR* szStringValue);

private:
	void SkipNoneCommand(void);
	bool GetNextWord(WCHAR** chppBuffer, int* ipLength);
	bool GetStringValue(WCHAR** chpBuffer, int* ipLength);

private:
	WCHAR* _bufferOrigin;
	WCHAR* _buffer;
};