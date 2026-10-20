// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QC_MEMORY_POOL_HPP
#define QC_MEMORY_POOL_HPP
#include "QC/Common/QCList.hpp"
#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryAllocatorIfs.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryPoolIfs.hpp"
#include <limits>
#include <string>
namespace QC
{
namespace Memory
{
class Pool : public QCMemoryPoolIfs
{
public:
    /**
     * @brief Constructor for Pool.
     * @return None.
     */
    Pool() = delete;
    explicit Pool( const QCMemoryPoolConfig_t &poolCfg ) : QCMemoryPoolIfs( poolCfg )
    {
        (void) QC_LOGGER_INIT( GetConfiguration().name.c_str(), LOGGER_LEVEL_ERROR );
    };
    ~Pool() override
    {
        std::lock_guard<std::mutex> lk( m_lock );
        QCBufferDescriptorBase_t descriptor = MakeDescriptorBase();
        QCStatus_e status = FreeObjects( m_freeObjects, descriptor );
        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to free pool objects status=%d", status );
        }
        status = FreeObjects( m_allocatedObjects, descriptor );
        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to free allocated pool objects status=%d", status );
        }
        (void) QC_LOGGER_DEINIT();
    }
    QCStatus_e Init() override
    {
        QCStatus_e status = QC_STATUS_OK;
        if ( ( 0U == GetConfiguration().maxElements ) ||
             ( GetConfiguration().maxElements > std::numeric_limits<uint32_t>::max() ) )
        {
            QC_ERROR( "Invalid maxElements %lu; supported range is 1 to %lu",
                      static_cast<unsigned long>( GetConfiguration().maxElements ),
                      static_cast<unsigned long>( std::numeric_limits<uint32_t>::max() ) );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            std::lock_guard<std::mutex> lk( m_lock );
            const uint32_t capacity = static_cast<uint32_t>( GetConfiguration().maxElements );
            status = m_freeObjects.Init( capacity );
            if ( QC_STATUS_OK == status )
            {
                status = m_allocatedObjects.Init( capacity );
            }
            if ( QC_STATUS_OK != status )
            {
                QC_ERROR( "Failed to initialize pool object lists status=%d", status );
            }
            else
            {
                QCBufferPropBase_t request;
                request.alignment = GetConfiguration().buff.alignment;
                request.cache = GetConfiguration().buff.cache;
                request.size = GetConfiguration().buff.size;

                for ( auto elementIndex = GetConfiguration().maxElements; elementIndex-- != 0; )
                {
                    QC_DEBUG( "Allocating %d out of %d elements", elementIndex, GetConfiguration().maxElements );
                    QCBufferDescriptorBase_t response;
                    QCStatus_e loopStatus = GetConfiguration().allocator.Allocate( request, response );
                    if ( QC_STATUS_OK != loopStatus )
                    {
                        status = loopStatus;
                        QC_ERROR( "Allocation failed with status %d", loopStatus );
                        break;
                    }
                    else
                    {
                        const PoolObject_t poolObject( response );
                        status = m_freeObjects.PushBack( poolObject );
                        if ( QC_STATUS_OK != status )
                        {
                            (void) GetConfiguration().allocator.Free( response );
                            QC_ERROR( "Failed to track pool object pBuf=%p dmaHandle=%ULL status=%d",
                                      poolObject.pBuf, poolObject.dmaHandle, status );
                            break;
                        }
                    }
                }
            }
        }
        return status;
    }
    QCStatus_e GetElement( QCBufferDescriptorBase_t &buffer ) override
    {
        QCStatus_e status = QC_STATUS_OK;
        std::lock_guard<std::mutex> lk( m_lock );
        bool isEmpty = false;
        status = m_freeObjects.Empty( isEmpty );
        if ( QC_STATUS_OK == status )
        {
            if ( isEmpty )
            {
                status = QC_STATUS_NO_RESOURCE;
            }
            else
            {
                PoolObject_t *freeObject = nullptr;
                status = m_freeObjects.Front( freeObject );
                if ( QC_STATUS_OK == status )
                {
                    if ( nullptr == freeObject->pBuf )
                    {
                        status = QC_STATUS_NULL_PTR;
                        QC_ERROR( "nullptr == buffer.pBuf" );
                    }
                    else
                    {
                        const PoolObject_t poolObject = *freeObject;
                        status = m_allocatedObjects.PushBack( poolObject );
                        if ( QC_STATUS_OK == status )
                        {
                            status = m_freeObjects.PopFront();
                            if ( QC_STATUS_OK != status )
                            {
                                (void) m_allocatedObjects.PopBack();
                            }
                        }
                        if ( QC_STATUS_OK == status )
                        {
                            QC_DEBUG( "extracted %p", poolObject.pBuf );
                            buffer = MakeDescriptorBase();
                            buffer.pBuf = poolObject.pBuf;
                            buffer.dmaHandle = poolObject.dmaHandle;
                        }
                        else
                        {
                            QC_ERROR( "Failed to move pool object pBuf=%p to allocated objects status=%d",
                                      poolObject.pBuf, status );
                        }
                    }
                }
            }
        }
        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to get free pool object status=%d", status );
        }
        return status;
    }
    QCStatus_e PutElement( const QCBufferDescriptorBase_t &buffer ) override
    {
        QCStatus_e status = QC_STATUS_BAD_ARGUMENTS;
        // check the match from allocator perspective
        if ( GetConfiguration().allocator.GetConfiguration().type != buffer.allocatorType )
        {
            QC_ERROR( "Allocator type mismatch expected %d recieved %d",
                      GetConfiguration().allocator.GetConfiguration().type, buffer.allocatorType );
        }
        else if ( nullptr == buffer.pBuf )
        {
            QC_ERROR( "nullptr == buffer.pBuf" );
            status = QC_STATUS_NULL_PTR;
        }
        else
        {
            std::lock_guard<std::mutex> lk( m_lock );
            const PoolObject_t poolObject( buffer );
            QCList<PoolObject_t>::Iterator current;
            QCList<PoolObject_t>::Iterator end;
            status = m_allocatedObjects.Begin( current );
            if ( QC_STATUS_OK == status )
            {
                status = m_allocatedObjects.End( end );
            }
            if ( QC_STATUS_OK == status )
            {
                status = QC_STATUS_BAD_ARGUMENTS;
                while ( end != current )
                {
                    if ( *current == poolObject )
                    {
                        status = m_freeObjects.PushBack( *current );
                        if ( QC_STATUS_OK == status )
                        {
                            QCList<PoolObject_t>::Iterator next;
                            status = m_allocatedObjects.Erase( current, next );
                            if ( QC_STATUS_OK != status )
                            {
                                (void) m_freeObjects.PopBack();
                                QC_ERROR( "Failed to remove pool object pBuf=%p from allocated objects status=%d",
                                          buffer.pBuf, status );
                            }
                        }
                        else
                        {
                            QC_ERROR( "Failed to add pool object pBuf=%p to free objects status=%d",
                                      buffer.pBuf, status );
                        }
                        break;
                    }
                    ++current;
                }
            }
            if ( QC_STATUS_OK != status )
            {
                QC_ERROR( "the pointer %p was not allocated from this pool", buffer.pBuf );
            }
        }
        return status;
    }

private:
    struct PoolObject
    {
        void *pBuf = nullptr;
        uint64_t dmaHandle = 0U;

        PoolObject() noexcept = default;
        explicit PoolObject( const QCBufferDescriptorBase_t &descriptor ) noexcept
            : pBuf( descriptor.pBuf ),
              dmaHandle( descriptor.dmaHandle )
        {}
        bool operator==( const PoolObject &rhs ) const noexcept
        {
            return ( pBuf == rhs.pBuf ) && ( dmaHandle == rhs.dmaHandle );
        }
    };

    using PoolObject_t = PoolObject;

    QCList<PoolObject_t> m_freeObjects;
    QCList<PoolObject_t> m_allocatedObjects;
    QC_DECLARE_LOGGER();

private:
    QCStatus_e FreeObjects( QCList<PoolObject_t> &objects, QCBufferDescriptorBase_t &descriptor )
    {
        QCStatus_e result = QC_STATUS_OK;
        PoolObject_t *poolObject = nullptr;
        while ( QC_STATUS_OK == objects.Front( poolObject ) )
        {
            descriptor.pBuf = poolObject->pBuf;
            descriptor.dmaHandle = poolObject->dmaHandle;
            const QCStatus_e freeStatus = GetConfiguration().allocator.Free( descriptor );
            if ( QC_STATUS_OK != freeStatus )
            {
                QC_ERROR( "Failed to free pool object pBuf=%p status=%d", descriptor.pBuf,
                          freeStatus );
                if ( QC_STATUS_OK == result )
                {
                    result = freeStatus;
                }
            }
            const QCStatus_e popStatus = objects.PopFront();
            if ( QC_STATUS_OK != popStatus )
            {
                QC_ERROR( "Failed to remove pool object pBuf=%p status=%d", descriptor.pBuf,
                          popStatus );
                if ( QC_STATUS_OK == result )
                {
                    result = popStatus;
                }
                break;
            }
        }
        return result;
    }

    QCBufferDescriptorBase_t MakeDescriptorBase()
    {
        QCBufferDescriptorBase_t d;
        d.alignment = GetConfiguration().buff.alignment;
        d.cache = GetConfiguration().buff.cache;
        d.size = GetConfiguration().buff.size;
        d.allocatorType = GetConfiguration().allocator.GetConfiguration().type;
        return d;
    }
};
}   // namespace Memory
}   // namespace QC
#endif   // QC_MEMORY_HEAP_ALLOCATOR_HPP
