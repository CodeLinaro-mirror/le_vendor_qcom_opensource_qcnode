// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/Ifs/QCMemoryFactory.hpp"

#include <cstdint>
#include <sstream>
#include <vector>

#include "QC/Infras/Memory/HeapAllocator.hpp"
#include "QC/Infras/Memory/ImageDescriptor.hpp"
#include "QC/Infras/Memory/ManagerLocal.hpp"
#include "QC/Infras/Memory/TensorDescriptor.hpp"

#if defined( __linux__ )
#include "QC/Infras/Memory/DMABUFFAllocator.hpp"
#elif defined( __QNX__ )
#include "QC/Infras/Memory/PMEMAllocator.hpp"
#endif

namespace QC
{
namespace Memory
{

// ---------------------------------------------------------------------------
// CreateAllocator
//
// HeapAllocator requires no device path.
// All DMA variants map to DMABUFFAllocator (Linux) or PMEMAllocator (QNX);
// the allocator type is forwarded so the implementation can open the correct
// device node.
//
// ISO 26262:6 Table 8 §1a : single return statement at end of function.
// ---------------------------------------------------------------------------
QCStatus_e QCMemoryFactory::CreateAllocator( QCMemoryAllocator_e type,
                                             const QCMemoryAllocatorConfigInit_t &config,
                                             std::unique_ptr<QCMemoryAllocatorIfs> &outAllocator )
{
    QCStatus_e status = QC_STATUS_OK;

    switch ( type )
    {
        case QC_MEMORY_ALLOCATOR_HEAP:
            outAllocator = std::make_unique<HeapAllocator>();
            break;

        case QC_MEMORY_ALLOCATOR_DMA:
        case QC_MEMORY_ALLOCATOR_DMA_CAMERA:
        case QC_MEMORY_ALLOCATOR_DMA_GPU:
        case QC_MEMORY_ALLOCATOR_DMA_VPU:
        case QC_MEMORY_ALLOCATOR_DMA_EVA:
        case QC_MEMORY_ALLOCATOR_DMA_HTP:
#if defined( __linux__ )
            outAllocator = std::make_unique<DMABUFFAllocator>( config, type );
#elif defined( __QNX__ )
            outAllocator = std::make_unique<PMEMAllocator>( config, type );
#else
            outAllocator.reset();
            status = QC_STATUS_UNSUPPORTED;
#endif
            break;

        default:
            outAllocator.reset();
            status = QC_STATUS_UNSUPPORTED;
            break;
    }

    return status;
}

// ---------------------------------------------------------------------------
// CreateBufferDescriptor
//
// ISO 26262:6 Table 8 §1a : single return statement at end of function.
// ---------------------------------------------------------------------------
QCStatus_e
QCMemoryFactory::CreateBufferDescriptor( QCBufferType_e type,
                                         std::unique_ptr<QCBufferDescriptorBase_t> &outDescriptor )
{
    QCStatus_e status = QC_STATUS_OK;

    switch ( type )
    {
        case QC_BUFFER_TYPE_IMAGE:
            outDescriptor = std::make_unique<ImageDescriptor_t>();
            break;

        case QC_BUFFER_TYPE_TENSOR:
            outDescriptor = std::make_unique<TensorDescriptor_t>();
            break;

        case QC_BUFFER_TYPE_RAW:
        case QC_BUFFER_TYPE_TXT:
            outDescriptor = std::make_unique<QCBufferDescriptorBase_t>();
            break;

        default:
            outDescriptor.reset();
            status = QC_STATUS_UNSUPPORTED;
            break;
    }

    return status;
}

// ---------------------------------------------------------------------------
// CreateMemoryManager
//
// ISO 26262:6 Table 8 §1a : single return statement at end of function.
// ---------------------------------------------------------------------------
QCStatus_e QCMemoryFactory::CreateMemoryManager( QCMemoryManagerType_e type,
                                                 std::unique_ptr<QCMemoryManagerIfs> &outManager )
{
    QCStatus_e status = QC_STATUS_OK;

    switch ( type )
    {
        case QC_MEMORY_MANAGER_LOCAL:
            outManager = std::make_unique<ManagerLocal>();
            if ( !outManager )
            {
                status = QC_STATUS_FAIL;
            }
            break;

        default:
            outManager.reset();
            status = QC_STATUS_UNSUPPORTED;
            break;
    }

    return status;
}

// ---------------------------------------------------------------------------
// GetSupportedAllocatorTypes
//
// DMA variants are included only on platforms where a DMA allocator is
// compiled in; they fall to QC_STATUS_UNSUPPORTED on all other platforms.
// ---------------------------------------------------------------------------
const std::vector<QCMemoryAllocator_e> &QCMemoryFactory::GetSupportedAllocatorTypes()
{
    static const std::vector<QCMemoryAllocator_e> kTypes = {
            QC_MEMORY_ALLOCATOR_HEAP,
#if defined( __linux__ ) || defined( __QNX__ )
            QC_MEMORY_ALLOCATOR_DMA,     QC_MEMORY_ALLOCATOR_DMA_CAMERA,
            QC_MEMORY_ALLOCATOR_DMA_GPU, QC_MEMORY_ALLOCATOR_DMA_VPU,
            QC_MEMORY_ALLOCATOR_DMA_EVA, QC_MEMORY_ALLOCATOR_DMA_HTP,
#endif
    };
    return kTypes;
}

// Returns JSON object of supported allocator types as key-value pairs (name: integer value)
std::string QCMemoryFactory::GetSupportedAllocatorTypesJson()
{
    static const std::pair<QCMemoryAllocator_e, const char *> kNames[] = {
            { QC_MEMORY_ALLOCATOR_HEAP, "QC_MEMORY_ALLOCATOR_HEAP" },
            { QC_MEMORY_ALLOCATOR_DMA, "QC_MEMORY_ALLOCATOR_DMA" },
            { QC_MEMORY_ALLOCATOR_DMA_CAMERA, "QC_MEMORY_ALLOCATOR_DMA_CAMERA" },
            { QC_MEMORY_ALLOCATOR_DMA_GPU, "QC_MEMORY_ALLOCATOR_DMA_GPU" },
            { QC_MEMORY_ALLOCATOR_DMA_VPU, "QC_MEMORY_ALLOCATOR_DMA_VPU" },
            { QC_MEMORY_ALLOCATOR_DMA_EVA, "QC_MEMORY_ALLOCATOR_DMA_EVA" },
            { QC_MEMORY_ALLOCATOR_DMA_HTP, "QC_MEMORY_ALLOCATOR_DMA_HTP" },
    };

    std::string json = "{";
    bool first = true;
    for ( const QCMemoryAllocator_e type : GetSupportedAllocatorTypes() )
    {
        for ( const auto &[enumVal, name] : kNames )
        {
            if ( enumVal == type )
            {
                if ( !first )
                {
                    json += ',';
                }
                json += '"';
                json += name;
                json += "\":";
                json += std::to_string( static_cast<int>( type ) );
                first = false;
                break;
            }
        }
    }
    json += '}';
    return json;
}

// ---------------------------------------------------------------------------
// GetSupportedBufferDescriptorTypes
// ---------------------------------------------------------------------------
const std::vector<QCBufferType_e> &QCMemoryFactory::GetSupportedBufferDescriptorTypes()
{
    static const std::vector<QCBufferType_e> kTypes = {
            QC_BUFFER_TYPE_TXT,
            QC_BUFFER_TYPE_RAW,
            QC_BUFFER_TYPE_IMAGE,
            QC_BUFFER_TYPE_TENSOR,
    };
    return kTypes;
}

// Returns JSON object of supported buffer descriptor types as key-value pairs (name: integer value)
std::string QCMemoryFactory::GetSupportedBufferDescriptorTypesJson()
{
    static const std::pair<QCBufferType_e, const char *> kNames[] = {
            { QC_BUFFER_TYPE_TXT, "QC_BUFFER_TYPE_TXT" },
            { QC_BUFFER_TYPE_RAW, "QC_BUFFER_TYPE_RAW" },
            { QC_BUFFER_TYPE_IMAGE, "QC_BUFFER_TYPE_IMAGE" },
            { QC_BUFFER_TYPE_TENSOR, "QC_BUFFER_TYPE_TENSOR" },
            { QC_BUFFER_TYPE_CUSTOM_0, "QC_BUFFER_TYPE_CUSTOM_0" },
            { QC_BUFFER_TYPE_CUSTOM_1, "QC_BUFFER_TYPE_CUSTOM_1" },
            { QC_BUFFER_TYPE_CUSTOM_2, "QC_BUFFER_TYPE_CUSTOM_2" },
            { QC_BUFFER_TYPE_CUSTOM_3, "QC_BUFFER_TYPE_CUSTOM_3" },
            { QC_BUFFER_TYPE_CUSTOM_4, "QC_BUFFER_TYPE_CUSTOM_4" },
    };

    std::string json = "{";
    bool first = true;
    for ( const QCBufferType_e type : GetSupportedBufferDescriptorTypes() )
    {
        for ( const auto &[enumVal, name] : kNames )
        {
            if ( enumVal == type )
            {
                if ( !first )
                {
                    json += ',';
                }
                json += '"';
                json += name;
                json += "\":";
                json += std::to_string( static_cast<int>( type ) );
                first = false;
                break;
            }
        }
    }
    json += '}';
    return json;
}

// ---------------------------------------------------------------------------
// GetSupportedMemoryManagerTypes
//
// Currently only the local (in-process) manager is available.
// Extend this list when remote or other manager variants are introduced.
// ---------------------------------------------------------------------------
const std::vector<QCMemoryManagerType_e> &QCMemoryFactory::GetSupportedMemoryManagerTypes()
{
    static const std::vector<QCMemoryManagerType_e> kTypes = {
            QC_MEMORY_MANAGER_LOCAL,
    };
    return kTypes;
}

// Returns JSON object of supported memory manager types as key-value pairs (name: integer value)
std::string QCMemoryFactory::GetSupportedMemoryManagerTypesJson()
{
    static const std::pair<QCMemoryManagerType_e, const char *> kNames[] = {
            { QC_MEMORY_MANAGER_LOCAL, "QC_MEMORY_MANAGER_LOCAL" },
    };

    std::string json = "{";
    bool first = true;
    for ( const QCMemoryManagerType_e type : GetSupportedMemoryManagerTypes() )
    {
        for ( const auto &[enumVal, name] : kNames )
        {
            if ( enumVal == type )
            {
                if ( !first )
                {
                    json += ',';
                }
                json += '"';
                json += name;
                json += "\":";
                json += std::to_string( static_cast<int>( type ) );
                first = false;
                break;
            }
        }
    }
    json += '}';
    return json;
}

}   // namespace Memory
}   // namespace QC
