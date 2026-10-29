// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/BufferDescriptor.hpp"

#include <utility>

namespace QC
{
namespace Memory
{

BufferDescriptor::BufferDescriptor( const BufferDescriptor &other ) noexcept
    : QCBufferDescriptorBase_t( other ),
      validSize( other.validSize ),
      offset( other.offset ),
      id( other.id )
{}

BufferDescriptor::BufferDescriptor( BufferDescriptor &&other ) noexcept
    : QCBufferDescriptorBase_t( std::move( other ) ),
      validSize( other.validSize ),
      offset( other.offset ),
      id( other.id )
{}

BufferDescriptor &BufferDescriptor::operator=( const BufferDescriptor &other ) noexcept
{
    if ( this != &other )
    {
        this->name = other.name;
        this->pBuf = other.pBuf;
        this->size = other.size;
        this->type = other.type;
        this->dmaHandle = other.dmaHandle;
        this->validSize = other.validSize;
        this->offset = other.offset;
        this->id = other.id;
        this->pid = other.pid;
        this->allocatorType = other.allocatorType;
        this->cache = other.cache;
    }
    return *this;
}

BufferDescriptor &BufferDescriptor::operator=( BufferDescriptor &&other ) noexcept
{
    if ( this != &other )
    {
        QCBufferDescriptorBase_t::operator=( std::move( other ) );
        validSize = other.validSize;
        offset = other.offset;
        id = other.id;
    }
    return *this;
}

BufferDescriptor &BufferDescriptor::operator=( const QCBufferDescriptorBase_t &other ) noexcept
{
    if ( this != &other )
    {
        this->name = other.name;
        this->pBuf = other.pBuf;
        this->size = other.size;
        this->type = other.type;
        this->dmaHandle = other.dmaHandle;
        this->validSize = other.size;
        this->offset = 0;
        this->id = UINT64_MAX;
        this->pid = other.pid;
        this->allocatorType = other.allocatorType;
        this->cache = other.cache;
    }
    return *this;
}

}   // namespace Memory

}   // namespace QC
