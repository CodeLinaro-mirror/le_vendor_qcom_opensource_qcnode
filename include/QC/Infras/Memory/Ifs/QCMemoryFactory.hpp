// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_MEMORY_FACTORY_HPP
#define QC_MEMORY_FACTORY_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "QC/Common/QCDefs.hpp"
#include "QC/Infras/Memory/Ifs/QCBufferDescriptorBase.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryAllocatorIfs.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryManagerIfs.hpp"

namespace QC
{
namespace Memory
{

// ---------------------------------------------------------------------------
// QCMemoryFactory
//
// Static factory methods for the three core memory abstractions, following
// the same convention as GFNodeAdaptor::CreateNode:
// ---------------------------------------------------------------------------
class QCMemoryFactory
{
public:
    // Returns the allocator implementation for the given type.
    // DMA variants dispatch to DMABUFFAllocator (Linux) or PMEMAllocator (QNX).
    // Returns QC_STATUS_UNSUPPORTED for unrecognised types; outAllocator is null.
    static QCStatus_e CreateAllocator( QCMemoryAllocator_e type,
                                       const QCMemoryAllocatorConfigInit_t &config,
                                       std::unique_ptr<QCMemoryAllocatorIfs> &outAllocator );

    // Returns a default-constructed descriptor subtype for the given buffer type:
    //   QC_BUFFER_TYPE_IMAGE  → ImageDescriptor_t
    //   QC_BUFFER_TYPE_TENSOR → TensorDescriptor_t
    //   QC_BUFFER_TYPE_RAW / QC_BUFFER_TYPE_TXT → QCBufferDescriptorBase_t
    // Returns QC_STATUS_UNSUPPORTED for CUSTOM_* types; outDescriptor is null.
    static QCStatus_e
    CreateBufferDescriptor( QCBufferType_e type,
                            std::unique_ptr<QCBufferDescriptorBase_t> &outDescriptor );

    // Returns a memory manager instance of the specified type.
    // Currently only QC_MEMORY_MANAGER_LOCAL is supported.
    // Returns QC_STATUS_UNSUPPORTED for unrecognised types; outManager is null.
    // Returns QC_STATUS_FAIL on allocation failure; outManager is null.
    static QCStatus_e CreateMemoryManager( QCMemoryManagerType_e type,
                                           std::unique_ptr<QCMemoryManagerIfs> &outManager );

    // ------------------------------------------------------------------
    // Supported-type queries
    // ------------------------------------------------------------------

    // Populates outTypes with allocator types constructible via CreateAllocator().
    // DMA variants are excluded on unsupported platforms.
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedAllocatorTypes( std::vector<QCMemoryAllocator_e> &outTypes );

    // Populates outTypes with buffer types constructible via CreateBufferDescriptor().
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedBufferDescriptorTypes( std::vector<QCBufferType_e> &outTypes );

    // Populates outTypes with memory manager types constructible via CreateMemoryManager().
    // Currently only QC_MEMORY_MANAGER_LOCAL is available; extend when remote
    // or other manager variants are introduced.
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedMemoryManagerTypes( std::vector<QCMemoryManagerType_e> &outTypes );

    // Populates outJson with a JSON object of allocator types constructible via CreateAllocator().
    // Returns QC_STATUS_OK on success; outJson is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedAllocatorTypesJson( std::string &outJson );

    // Populates outJson with a JSON object of buffer descriptor types constructible via CreateBufferDescriptor().
    // Returns QC_STATUS_OK on success; outJson is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedBufferDescriptorTypesJson( std::string &outJson );

    // Populates outJson with a JSON object of memory manager types constructible via CreateMemoryManager().
    // Returns QC_STATUS_OK on success; outJson is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedMemoryManagerTypesJson( std::string &outJson );

private:
    QCMemoryFactory() = delete;
};

}   // namespace Memory
}   // namespace QC

#endif   // QC_MEMORY_FACTORY_HPP
