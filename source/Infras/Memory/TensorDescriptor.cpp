// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/TensorDescriptor.hpp"
#include "QC/Infras/Log/Logger.hpp"

#include <utility>

namespace QC
{
namespace Memory
{

TensorDescriptor::TensorDescriptor( const TensorDescriptor &other ) noexcept
    : BufferDescriptor( other ), tensorType( other.tensorType ), numDims( other.numDims )
{
    (void) std::copy_n( std::begin( other.dims ), QC_NUM_TENSOR_DIMS, std::begin( dims ) );
}

TensorDescriptor::TensorDescriptor( TensorDescriptor &&other ) noexcept
    : BufferDescriptor( std::move( other ) ),
      tensorType( other.tensorType ),
      numDims( other.numDims )
{
    (void) std::copy_n( std::begin( other.dims ), QC_NUM_TENSOR_DIMS, std::begin( dims ) );
}

TensorDescriptor &TensorDescriptor::operator=( const BufferDescriptor &other ) noexcept
{
    BufferDescriptor::operator=( other );
    this->type = QC_BUFFER_TYPE_TENSOR;
    return *this;
}

TensorDescriptor &TensorDescriptor::operator=( const TensorDescriptor &other ) noexcept
{
    if ( this != &other )
    {
        BufferDescriptor::operator=( other );
        this->type = QC_BUFFER_TYPE_TENSOR;
        this->tensorType = other.tensorType;
        const uint32_t dimensionCount = std::min( other.numDims, (uint32_t) QC_NUM_TENSOR_DIMS );
        (void) std::copy_n( std::begin( other.dims ), dimensionCount, std::begin( this->dims ) );
        this->numDims = dimensionCount;
    }
    return *this;
}

TensorDescriptor &TensorDescriptor::operator=( TensorDescriptor &&other ) noexcept
{
    if ( this != &other )
    {
        BufferDescriptor::operator=( std::move( other ) );
        tensorType = other.tensorType;
        (void) std::copy_n( std::begin( other.dims ), QC_NUM_TENSOR_DIMS,
                            std::begin( dims ) );
        numDims = other.numDims;
    }
    return *this;
}

TensorDescriptor &TensorDescriptor::operator=( const QCBufferDescriptorBase_t &other ) noexcept
{
    if ( this != &other )
    {
        const TensorDescriptor_t *pTensorDesc = dynamic_cast<const TensorDescriptor_t *>( &other );
        const BufferDescriptor_t *pBufDesc = dynamic_cast<const BufferDescriptor_t *>( &other );
        if ( nullptr != pTensorDesc )
        {
            TensorDescriptor::operator=( *pTensorDesc );
        }
        else if ( nullptr != pBufDesc )
        {
            TensorDescriptor::operator=( *pBufDesc );
        }
        else
        {
            BufferDescriptor::operator=( other );
        }
    }
    return *this;
}

}   // namespace Memory

}   // namespace QC
