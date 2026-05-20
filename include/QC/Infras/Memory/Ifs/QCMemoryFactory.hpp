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

    // Returns the allocator types constructible via CreateAllocator() on the
    // current platform. DMA variants are excluded on unsupported platforms.
    static const std::vector<QCMemoryAllocator_e> &GetSupportedAllocatorTypes();

    // Returns the buffer types constructible via CreateBufferDescriptor().
    static const std::vector<QCBufferType_e> &GetSupportedBufferDescriptorTypes();

    // Returns the memory manager types constructible via CreateMemoryManager().
    // Currently only QC_MEMORY_MANAGER_LOCAL is available; extend when remote
    // or other manager variants are introduced.
    static const std::vector<QCMemoryManagerType_e> &GetSupportedMemoryManagerTypes();

    // Returns the allocator types json
    static std::string GetSupportedAllocatorTypesJson();

    // Returns the supported buffer descriptor types json
    static std::string GetSupportedBufferDescriptorTypesJson();

    // Returns the supported memory manager types json
    static std::string GetSupportedMemoryManagerTypesJson();

private:
    QCMemoryFactory() = delete;
};

}   // namespace Memory
}   // namespace QC

#endif   // QC_MEMORY_FACTORY_HPP
