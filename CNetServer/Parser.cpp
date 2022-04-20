#include "pch.h"
#include "Parser.h"

Parser::Parser()
	:_bufferOrigin(nullptr), _buffer(nullptr)
{
}

Parser::~Parser()
{
	if (_bufferOrigin != nullptr)
	{
		_bufferOrigin--;
		delete _bufferOrigin;
	}

	_bufferOrigin = nullptr;
}

bool Parser::LoadFile(const WCHAR* szFileName)
{
	FILE* pFile;
	errno_t err = _wfopen_s(&pFile, szFileName, L"rt");
	if (err != 0 || pFile == nullptr)
		return false;

	int iFileSize;
	fseek(pFile, 0, SEEK_END);
	iFileSize = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	_bufferOrigin = new WCHAR[iFileSize];
	fread_s(_bufferOrigin, iFileSize, iFileSize, 1, pFile);

	_bufferOrigin++;

	_buffer = _bufferOrigin;
	fclose(pFile);
}

bool Parser::GetValue(const WCHAR* szName, int* ipValue)
{
	WCHAR* pWord;
	WCHAR szWord[256];
	int iLength = 0;

	_buffer = _bufferOrigin;

	while (_buffer)
	{
		GetNextWord(&pWord, &iLength);
		szWord[iLength] = L'\0';
		wmemcpy(szWord, pWord, iLength);

		if (!wcscmp(szName, szWord))
		{
			GetNextWord(&pWord, &iLength);
			szWord[iLength] = L'\0';
			wmemcpy(szWord, pWord, iLength);

			if (!wcscmp(L":", szWord))
			{
				GetNextWord(&pWord, &iLength);
				szWord[iLength] = L'\0';
				wmemcpy(szWord, pWord, iLength);
				*ipValue = _wtoi(szWord);
				return true;
			}
			return false;
		}
	}
	return false;
}

bool Parser::GetString(const WCHAR* szName, WCHAR* szStringValue)
{
	WCHAR* pWord;
	WCHAR szWord[256];
	int iLength = 0;

	_buffer = _bufferOrigin;

	while (_buffer)
	{
		GetNextWord(&pWord, &iLength);
		szWord[iLength] = L'\0';
		wmemcpy(szWord, pWord, iLength);

		if (!wcscmp(szName, szWord))
		{
			GetNextWord(&pWord, &iLength);
			szWord[iLength] = '\0';
			wmemcpy(szWord, pWord, iLength);

			if (!wcscmp(L":", szWord))
			{
				GetStringValue(&pWord, &iLength);
				szWord[iLength] = '\0';
				wmemcpy(szWord, pWord, iLength);
				wmemcpy(szStringValue, szWord, iLength + 1);
				return true;
			}
			return false;
		}
	}

	return false;
}

void Parser::SkipNoneCommand(void)
{
	while (true)
	{
		if (*_buffer == L'/' && *(_buffer + 1) == L'/')
		{
			while (*_buffer != L'\n')
			{
				_buffer++;
			}
			_buffer++;
		}
		else if (*_buffer == L'/' && *(_buffer + 1) == L'*')
		{
			while (false == (*_buffer == L'*' && *(_buffer + 1) == L'/'))
			{
				_buffer++;
			}
			_buffer += 2;
		}
		else if (*_buffer == L',' || *_buffer == L'.' || *_buffer == L'"' || *_buffer == 0x20
			|| *_buffer == 0x08 || *_buffer == 0x09 || *_buffer == 0x0a || *_buffer == 0x0d
			|| *_buffer == L'{' || *_buffer == L'}')
		{
			_buffer++;
		}
		else
			break;
	}
}

bool Parser::GetNextWord(WCHAR** chppBuffer, int* ipLength)
{
	SkipNoneCommand();
	*ipLength = 0;

	*chppBuffer = _buffer;

	while (iswdigit(*_buffer) || iswalpha(*_buffer) || *_buffer == ':')
	{
		_buffer++;
		(*ipLength)++;
	}
	return true;
}

bool Parser::GetStringValue(WCHAR** chppBuffer, int* ipLength)
{
	SkipNoneCommand();
	*ipLength = 0;

	*chppBuffer = _buffer;

	while (*_buffer != L'"')
	{
		_buffer++;
		(*ipLength)++;
	}
	return true;
}
