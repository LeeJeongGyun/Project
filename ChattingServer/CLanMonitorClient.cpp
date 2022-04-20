#include "pch.h"
#include "CLanMonitorClient.h"

CLanMonitorClient::CLanMonitorClient()
{
	_parser.LoadFile(L"ChattingServer.txt");
	WCHAR szBindIP[dfIP_LEN];
	WCHAR szServerIP[dfIP_LEN];
	int32 serverPort;
	int32 numOfWorkerThread;
	int32 bNagleOn;

	_parser.GetString(L"MONITOR_BIND_IP", szBindIP);
	_parser.GetString(L"MONITOR_SERVER_IP", szServerIP);
	_parser.GetValue(L"MONITOR_SERVER_PORT", &serverPort);
	_parser.GetValue(L"MONITOR_IOCP_WORKER_THREAD", &numOfWorkerThread);
	_parser.GetValue(L"MONITOR_NAGLE_ON", &bNagleOn);

	CLanClient::Connect(szBindIP, szServerIP, serverPort, numOfWorkerThread, bNagleOn);
}

CLanMonitorClient::~CLanMonitorClient()
{
}

void CLanMonitorClient::OnEnterJoinServer()
{
}

void CLanMonitorClient::OnLeaveServer()
{
}

void CLanMonitorClient::OnRecv(Packet* pPacket)
{
}

void CLanMonitorClient::OnSend(int iSendSize)
{
}

void CLanMonitorClient::OnError(int iErrCode, WCHAR* szStr)
{
}

void CLanMonitorClient::OnSetLoginPacket(Packet* pPacket)
{
	mp_MonitorLogin(pPacket, en_PACKET_SS_MONITOR_LOGIN, 1);
	stPacketHeaderLan header;
	header.len = pPacket->GetUseSize() - sizeof(stPacketHeaderLan);
	pPacket->InputHeadData((char*)&header, sizeof(stPacketHeaderLan));
}

void CLanMonitorClient::mp_MonitorLogin(Packet* pPacket, uint16 type, int32 serverNo)
{
	pPacket->Clear();
	pPacket->ReserveHeadSize(sizeof(stPacketHeaderLan));
	*pPacket << type << serverNo;
}

void CLanMonitorClient::mp_MonitorData(Packet* pPacket, uint16 type, uint8 dataType, int32 dataValue, int32 timeStamp)
{
	pPacket->Clear();
	pPacket->ReserveHeadSize(sizeof(stPacketHeaderLan));
	*pPacket << type << dataType << dataValue << timeStamp;
}
