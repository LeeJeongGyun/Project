#pragma once

#include "CLanClient.h"

class CLanMonitorClient : public CLanClient
{
public:
	CLanMonitorClient();
	~CLanMonitorClient();

	void OnEnterJoinServer() override;
	void OnLeaveServer() override;

	void OnRecv(Packet* pPacket) override;
	void OnSend(int iSendSize) override;

	void OnError(int iErrCode, WCHAR* szStr) override;
	void OnSetLoginPacket(Packet* pPacket) override;

	void mp_MonitorLogin(Packet* pPacket, uint16 type, int32 serverNo);
	void mp_MonitorData(Packet* pPacket, uint16 type, uint8 dataType, int32 dataValue, int32 timeStamp);

private:
	Parser		_parser;
};

