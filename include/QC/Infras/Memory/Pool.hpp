// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QC_MEMORY_POOL_HPP
#define QC_MEMORY_POOL_HPP
#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryAllocatorIfs.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryPoolIfs.hpp"
#include <algorithm>
#include <functional>
#include <list>
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
    Pool( const QCMemoryPoolConfig_t &poolCfg ) : QCMemoryPoolIfs( poolCfg )
    {
        (void) QC_LOGGER_INIT( GetConfiguration().name.c_str(), LOGGER_LEVEL_ERROR );
    };
    ~Pool() override
    {
        std::lock_guard<std::mutex> lk( m_lock );
        QCBufferDescriptorBase_t descriptor = MakeDescriptorBase();
        for ( auto it = m_freeObjects.begin(); it != m_freeObjects.end(); ++it )
        {
            descriptor.pBuf = it->pBuf;
            descriptor.dmaHandle = it->dmaHandle;
            GetConfiguration().allocator.Free( descriptor );
        }
        for ( auto it = m_allocatedObjects.begin(); it != m_allocatedObjects.end(); ++it )
        {
            descriptor.pBuf = it->pBuf;
            descriptor.dmaHandle = it->dmaHandle;
            GetConfiguration().allocator.Free( descriptor );
        }
        QC_LOGGER_DEINIT();
    }
    virtual QCStatus_e Init()
    {
        QCStatus_e status = QC_STATUS_OK;
        if ( 0 == GetConfiguration().maxElements )
        {
            QC_ERROR( "0 == m_config.m_maxElements" );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            QCBufferPropBase_t request;
            request.alignment = GetConfiguration().buff.alignment;
            request.cache = GetConfiguration().buff.cache;
            request.size = GetConfiguration().buff.size;
            // the below can reduce effectiveness of pool creations
            // in case of multiple creations
            std::lock_guard<std::mutex> lk( m_lock );
            for ( auto _ = GetConfiguration().maxElements; _--; )
            {
                QC_DEBUG( "Allocating %d out of %d elements", _, GetConfiguration().maxElements );
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
                    poolObjectDB_t poolObj = response;
                    m_freeObjects.push_back( poolObj );
                    // verify insertion DB correctness
                    if ( m_freeObjects.back() != poolObj )
                    {
                        status = QC_STATUS_FAIL;
                        QC_ERROR( "m_freeObjects.back() != poolObj, pBuf=%p dmaHandle=%ULL",
                                  poolObj.pBuf, poolObj.dmaHandle );
                        break;
                    }
                }
            }
        }
        return status;
    }
    virtual QCStatus_e GetElement( QCBufferDescriptorBase_t &buffer )
    {
        QCStatus_e status = QC_STATUS_OK;
        std::lock_guard<std::mutex> lk( m_lock );
        if ( !m_freeObjects.empty() )
        {
            poolObjectDB_t poolObj = m_freeObjects.front();
            if ( nullptr == poolObj.pBuf )
            {
                status = QC_STATUS_NULL_PTR;
                QC_ERROR( "nullptr == buffer.pBuf" );
            }
            else
            {
                m_freeObjects.pop_front();
                QC_DEBUG( "extracted %p", poolObj.pBuf );
                m_allocatedObjects.push_back( poolObj );
                buffer = MakeDescriptorBase();
                buffer.pBuf = poolObj.pBuf;
                buffer.dmaHandle = poolObj.dmaHandle;
                auto findIt = std::find( m_freeObjects.begin(), m_freeObjects.end(), poolObj );
                // removal from free objects data base verification
                if ( findIt != m_freeObjects.end() )
                {
                    status = QC_STATUS_FAIL;
                    QC_ERROR( "m_freeObjects.find(%p) != m_freeObjects.end()", buffer.pBuf );
                }
                // addition to allocated objects data base verification
                else if ( m_allocatedObjects.back() != buffer )
                {
                    status = QC_STATUS_FAIL;
                    QC_ERROR( "m_allocatedObjects.back()!= buffer.pBuf %p", buffer.pBuf );
                }
                else
                {
                }
            }
        }
        else
        {
            status = QC_STATUS_NO_RESOURCE;
            QC_ERROR( "No resources in pool" );
        }
        return status;
    }
    virtual QCStatus_e PutElement( const QCBufferDescriptorBase_t &buffer )
    {
        QCStatus_e status = QC_STATUS_BAD_ARGUMENTS;
        bool inDataBase = false;
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
            for ( auto it = m_allocatedObjects.begin(); it != m_allocatedObjects.end(); ++it )
            {
                poolObjectDB_t pollObj = *it;
                if ( pollObj == buffer )
                {
                    inDataBase = true;
                    m_allocatedObjects.erase( it );
                    m_freeObjects.push_back( pollObj );
                    status = QC_STATUS_OK;
                    // removal from allocated objects data base verification
                    auto findIt = std::find( m_allocatedObjects.begin(), m_allocatedObjects.end(),
                                             pollObj );
                    if ( findIt != m_allocatedObjects.end() )
                    {
                        status = QC_STATUS_FAIL;
                        QC_ERROR( "m_allocatedObjects.find(%p) != m_allocatedObjects.end()",
                                  buffer.pBuf );
                    }
                    // addition to allocated objects data base verification
                    else if ( m_freeObjects.back() != pollObj )
                    {
                        status = QC_STATUS_FAIL;
                        QC_ERROR( "m_freeObjects.back() != pollObj.pBuf %p", pollObj.pBuf );
                    }
                    else
                    {
                    }
                    break;
                }
            }
            if ( false == inDataBase )
            {
                QC_ERROR( "the pointer %p was not allocated from this pool", buffer.pBuf );
            }
        }
        return status;
    }

private:
    typedef struct poolObjectDB
    {
        void *pBuf = nullptr;
        uint64_t dmaHandle = 0;
        poolObjectDB() = default;
        // operators between QCBufferDescriptorBase_t to poolObjectDB
        poolObjectDB( const QCBufferDescriptorBase_t &rhs )
            : pBuf( rhs.pBuf ),
              dmaHandle( rhs.dmaHandle ) {};
        poolObjectDB &operator=( const QCBufferDescriptorBase_t &rhs )
        {
            pBuf = rhs.pBuf;
            dmaHandle = rhs.dmaHandle;
            return *this;
        }
        bool operator==( const QCBufferDescriptorBase_t &rhs ) const
        {
            return pBuf == rhs.pBuf && dmaHandle == rhs.dmaHandle;
        }
        bool operator!=( const QCBufferDescriptorBase_t &rhs ) { return !( *this == rhs ); }
        // operators between poolObjectDB to itself
        bool operator==( const poolObjectDB &rhs ) const noexcept
        {
            return pBuf == rhs.pBuf && dmaHandle == rhs.dmaHandle;
        }
        bool operator!=( const poolObjectDB &rhs ) const noexcept { return !( *this == rhs ); }
        bool operator<( const poolObjectDB &rhs ) const
        {
            // Keep only ONE operator< now
            return ( pBuf < rhs.pBuf ) || ( pBuf == rhs.pBuf && dmaHandle < rhs.dmaHandle );
        }
    } poolObjectDB_t;
    std::list<poolObjectDB_t> m_freeObjects;
    std::list<poolObjectDB_t> m_allocatedObjects;
    QC_DECLARE_LOGGER();

private:
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