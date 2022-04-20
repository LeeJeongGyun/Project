#include "pch.h"
#include "PacketPtr.h"

PacketPtr::PacketPtr()
    :_ptr(nullptr)
{
}

PacketPtr::PacketPtr(Packet* ptr)
{
    _ptr = ptr;
}

PacketPtr::PacketPtr(const PacketPtr& rhs)
{
    if (_ptr != nullptr)
    {
        if (0 == _ptr->SubRef())
            Packet::Free(_ptr);
    }

    _ptr = rhs._ptr;
    _ptr->AddRef();
}

PacketPtr::~PacketPtr()
{
    if (_ptr != nullptr)
    {
        if (_ptr->SubRef() == 0)
            Packet::Free(_ptr);
    }

    _ptr = nullptr;
}

PacketPtr& PacketPtr::operator=(const PacketPtr& rhs)
{
    if (_ptr != nullptr)
    {
        if (_ptr->SubRef() == 0)
            Packet::Free(_ptr);
    }

    _ptr = rhs._ptr;
    _ptr->AddRef();

    return *this;
}

PacketPtr& PacketPtr::operator=(PacketPtr&& ptr)
{
    if (_ptr != nullptr)
    {
        if (_ptr->SubRef() == 0)
            Packet::Free(_ptr);
    }

    _ptr = ptr._ptr;
    ptr._ptr = nullptr;
    return *this;
}