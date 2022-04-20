#pragma once

#include "Packet.h"

class PacketPtr
{
public:
	PacketPtr();
	PacketPtr(Packet* ptr);
	PacketPtr(const PacketPtr& rhs);
	~PacketPtr();

	PacketPtr& operator=(const PacketPtr& rhs);
	PacketPtr& operator=(PacketPtr&& ptr);

	template<typename T>
	PacketPtr& operator<<(const T& data);

	template<typename T>
	PacketPtr& operator>>(T& data);

public:
	Packet* _ptr = nullptr;
};

template<typename T>
inline PacketPtr& PacketPtr::operator<<(const T& data)
{
	*_ptr << data;
	return *this;
}

template<typename T>
inline PacketPtr& PacketPtr::operator>>(T& data)
{
	*_ptr >> data;
	return *this;
}
