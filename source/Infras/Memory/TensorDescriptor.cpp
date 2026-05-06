// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/TensorDescriptor.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{
namespace Memory
{

TensorDescriptor &TensorDescriptor::operator=( const BufferDescriptor &other )
{
    BufferDescriptor::operator=( other );
    this->type = QC_BUFFER_TYPE_TENSOR;
    return *this;
}

TensorDescriptor &TensorDescriptor::operator=( const TensorDescriptor &other )
{
    if ( this != &other )
    {
        BufferDescriptor::operator=( other );
        this->type = QC_BUFFER_TYPE_TENSOR;
        this->tensorType = other.tensorType;
        uint32_t numDims = std::min( other.numDims, (uint32_t) QC_NUM_TENSOR_DIMS );
        std::copy( other.dims, other.dims + numDims, this->dims );
        this->numDims = numDims;
    }
    return *this;
}

TensorDescriptor &TensorDescriptor::operator=( const QCBufferDescriptorBase_t &other )
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
