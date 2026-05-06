// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/ManagerLocal.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/Pool.hpp"
#include <cstdlib>
#include <random>
#include <unistd.h>


namespace QC
{
namespace Memory
{

ManagerLocal::ManagerLocal()
{
    SetState( QC_OBJECT_STATE_INITIAL );
    (void) QC_LOGGER_INIT( "ManagerLocal", LOGGER_LEVEL_ERROR );
}

QCStatus_e ManagerLocal::Initialize( const QCMemoryManagerInit_t &init )
{
    QCStatus_e status = QC_STATUS_OK;
    // reuse of member mutex for this function scope only
    // scoped lock
    std::lock_guard<std::mutex> lk( m_multiThreadLock );
    QCObjectState_e state = GetState();
    if ( QC_OBJECT_STATE_INITIAL != state )
    {
        QC_ERROR( "QC_OBJECT_STATE_INITIAL != m_state" );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( 0 == init.numOfNodes )
    {
        QC_ERROR( "0 ==  init.numOfNodes" );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        m_config = init;
        QC_DEBUG( "SetState( QC_OBJECT_STATE_INITIALIZING)" );
        SetState( QC_OBJECT_STATE_INITIALIZING );
    }

    if ( QC_OBJECT_STATE_INITIALIZING == GetState() )
    {
        m_pools.reserve( m_config.numOfNodes );
        m_allocations.reserve( m_config.numOfNodes );
        for ( auto i = 0; i < m_config.numOfNodes; i++ )
        {
            m_pools.emplace_back();
            m_allocations.emplace_back();
        }

        state = QC_OBJECT_STATE_READY;
    }

    QC_DEBUG( "SetState( %d)", state );
    SetState( state );

    return status;
}

ManagerLocal::~ManagerLocal()
{
    if ( GetState() == QC_OBJECT_STATE_READY )
    {
        DeInitialize();
    }

    (void) QC_LOGGER_DEINIT();
}

QCStatus_e ManagerLocal::DeInitialize()
{
    // Free all allocated resources if not freed before
    // reuse of member mutex for this function scope only
    // scoped lock
    std::lock_guard<std::mutex> lk( m_multiThreadLock );
    QCStatus_e status = QC_STATUS_OK;
    QCObjectState_e state = QC_OBJECT_STATE_INITIAL;
    if ( GetState() == QC_OBJECT_STATE_INITIAL )
    {
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        SetState( QC_OBJECT_STATE_DEINITIALIZING );
        QC_DEBUG( "SetState( QC_OBJECT_STATE_DEINITIALIZING );" );
        for ( auto i = m_handleToNodeIdInVector.begin(); i != m_handleToNodeIdInVector.end(); i++ )
        {
            QCStatus_e statusLocal = ReclaimResources( i->first );
            QC_INFO( "ReclaimResources for QCMemoryHandle_t %d returned %d", i->first, status );
            if ( QC_STATUS_OK != statusLocal )
            {
                state = QC_OBJECT_STATE_ERROR;
                status = statusLocal;
                QC_ERROR( "ReclaimResources for QCMemoryHandle_t %d returned %d", i->first,
                          status );
            }
        }

        // Clear the vectors (this will properly destruct the objects)
        for ( auto i = 0; i < m_config.numOfNodes; i++ )
        {
            QC_DEBUG( "DB memory free itteration %d ", i );
            PoolMapWithMutex &refPoolMapWithMutex = m_pools.back();
            QC_DEBUG( "refPoolMapWithMutex at %p ", &refPoolMapWithMutex );
            m_pools.pop_back();

            AllocationSetWithMutex &refAllocSetWithMutex = m_allocations.back();
            QC_DEBUG( "refAllocSetWithMutex at %p ", &refAllocSetWithMutex );
            m_allocations.pop_back();
        }

        m_config = QCMemoryManagerInit();
        QC_DEBUG( "SetState( %d)", state );
        SetState( state );
    }

    return status;
}

// Un/Registration
QCStatus_e ManagerLocal::Register( const QCNodeID_t &node, QCMemoryHandle_t &handle )
{
    QC_INFO( "node name %s node type %d node id %d", node.name.c_str(), node.type, node.id );

    QCStatus_e status = QC_STATUS_OK;

    // Initial validation without locks
    if ( GetState() != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", GetState() );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( ( node.type >= QC_NODE_TYPE_LAST ) || ( node.type == QC_NODE_TYPE_RESERVED ) )
    {
        QC_ERROR( "(node.type >= QC_NODE_TYPE_LAST(%d) ) || ( node.type == "
                  "QC_NODE_TYPE_RESERVED(%d) )",
                  QC_NODE_TYPE_LAST, QC_NODE_TYPE_RESERVED );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( m_config.numOfNodes <= node.id )
    {
        QC_ERROR( "node.id %d is bigger or equal to m_config.numOfNodes %d", node.id,
                  m_config.numOfNodes );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        // Generate random number for memory handlers
        std::random_device rd;
        std::mt19937 randomNumbersGenerator( rd() );
        std::uniform_int_distribution<uint32_t> distribution( 0, UINT32_MAX );

        // Set Node enum into memory handler
        handle.SetNodeType( node.type );
        QC_DEBUG( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 " ",
                  handle.GetNodeType(), handle.GetNodeCount(), handle.GetRandomNumber(),
                  handle.GetProcessId() );

        // Critical section with unique lock for handle registration
        {
            std::unique_lock<std::shared_mutex> writeLock( m_handle2NodeIdLock );

            // Check capacity limits
            if ( m_handleToNodeIdInVector.size() >= m_config.numOfNodes )
            {
                QC_ERROR( "m_handleToNodeIdInVector.size() >= m_config.numOfNodes (%zu >= %d)",
                          m_handleToNodeIdInVector.size(), m_config.numOfNodes );
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            // Check if node ID is unique
            else if ( IsNodeIdUnique( node ) == false )
            {
                QC_ERROR( "node.id %d is already registered in m_handleToNodeIdInVector", node.id );
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else
            {
                // Set node count based on current registry size
                handle.SetNodeCount( m_handleToNodeIdInVector.size() + 1 );

                // Generate and set random number
                handle.SetRandomNumber( distribution( randomNumbersGenerator ) );

                QC_DEBUG( "Memory Handle node type %d count %d random Number %" PRIu32
                          " pid %" PRIu32 " ",
                          handle.GetNodeType(), handle.GetNodeCount(), handle.GetRandomNumber(),
                          handle.GetProcessId() );

                // Check if generated handle already exists (collision detection)
                uint8_t tempNodeId;
                if ( true == IsMemoryHandleRegistered( handle, tempNodeId ) )
                {
                    QC_ERROR( "COULD NOT GENERATE UNIQUE HANDLE - collision detected" );
                    status = QC_STATUS_FAIL;
                }
                else
                {
                    // Insert the new handle-to-node mapping
                    m_handleToNodeIdInVector.insert( { handle, node.id } );
                    if ( false == IsMemoryHandleRegistered( handle, tempNodeId ) )
                    {
                        QC_ERROR( "Failed to insert handle into registry" );
                        status = QC_STATUS_FAIL;
                    }
                }
            }
        }
    }

    if ( QC_STATUS_FAIL == status )
    {
        QC_ERROR( "m_state = QC_OBJECT_STATE_ERROR" );
        SetState( QC_OBJECT_STATE_ERROR );
    }

    return status;
}

QCStatus_e ManagerLocal::UnRegister( const QCMemoryHandle_t &memHandle )
{
    QCStatus_e status = QC_STATUS_OK;
    uint8_t nodeId;
    QCObjectState_e startState = GetState();
    QCObjectState_e finalState = startState;

    // Initial validation with shared lock for handle lookup
    {
        std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
        if ( startState != QC_OBJECT_STATE_READY )
        {
            QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", startState );
            status = QC_STATUS_BAD_STATE;
        }
        else if ( false == IsMemoryHandleRegistered( memHandle, nodeId ) )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32
                      " ",
                      memHandle.GetNodeType(), memHandle.GetNodeCount(),
                      memHandle.GetRandomNumber(), memHandle.GetProcessId() );
        }
    }

    // If validation passed, proceed with resource cleanup and handle removal
    if ( status == QC_STATUS_OK )
    {
        QC_DEBUG( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 " ",
                  memHandle.GetNodeType(), memHandle.GetNodeCount(), memHandle.GetRandomNumber(),
                  memHandle.GetProcessId() );

        finalState = QC_OBJECT_STATE_ERROR;

        // Clean all allocations related to this handle
        status = ReclaimResources( memHandle );

        if ( status == QC_STATUS_OK )
        {
            finalState = startState;
            // Remove handle from registry with unique lock
            {
                std::unique_lock<std::shared_mutex> writeLock( m_handle2NodeIdLock );
                m_handleToNodeIdInVector.erase( memHandle );
                if ( true == IsMemoryHandleRegistered( memHandle, nodeId ) )
                {
                    // erasure failed
                    finalState = QC_OBJECT_STATE_ERROR;
                    status = QC_STATUS_FAIL;
                    QC_ERROR( "Memory Handle still registered after erase - node type %d count %d "
                              "random Number %" PRIu32 " pid %" PRIu32 " ",
                              memHandle.GetNodeType(), memHandle.GetNodeCount(),
                              memHandle.GetRandomNumber(), memHandle.GetProcessId() );
                }
            }
        }
    }

    QC_DEBUG( "GetState () == %d", finalState );
    SetState( finalState );

    return status;
}

// Pools methods
// Pools creation and destruction
QCStatus_e ManagerLocal::CreatePool( const QCMemoryHandle_t &handle,
                                     const QCMemoryPoolInitConfig_t &poolCfg,
                                     QCMemoryPoolHandle_t &poolHandle )
{
    QCStatus_e status = QC_STATUS_OK;
    QCObjectState_e state = GetState();
    // check Memory Handle correctness
    uint8_t nodeIndex;
    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( state != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", state );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( false == IsMemoryHandleRegistered( handle, nodeIndex ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 " ",
                  handle.GetNodeType(), handle.GetNodeCount(), handle.GetRandomNumber(),
                  handle.GetProcessId() );
    }
    else if ( QC_MEMORY_ALLOCATOR_LAST <= poolCfg.allocator )
    {
        QC_ERROR( "QC_MEMORY_ALLOCATOR_LAST(%d) <= allocator, allocator=%d",
                  QC_MEMORY_ALLOCATOR_LAST, poolCfg.allocator );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        readLock.unlock();

        // Use the individual pool's mutex for thread-safe access
        std::unique_lock<std::shared_mutex> poolLock( m_pools[nodeIndex].poolMutex );

        if ( QC_MEMORY_MAX_POOLS_PER_NODE == m_pools[nodeIndex].poolMap.size() )
        {
            status = QC_STATUS_OUT_OF_BOUND;
            QC_ERROR( "Node m_pools count too hight %" PRIu64 " ", UINT8_MAX );
        }
        else
        {
            // generate random number to used as memory handlers
            //  create random device
            std::random_device rd;
            // create Mersenne Twister engine for 64-bit integers
            // with random device as seed value
            std::mt19937 randomNumbersGenerator( rd() );
            // define result range uint64 and distribution
            std::uniform_int_distribution<uint64_t> distribution( 0, UINT32_MAX );
            // check uniqness

            // Set memory handler into pool handler
            poolHandle.SetMemoryHandle( handle );
            // generate and set random number
            poolHandle.SetRandomNumber( distribution( randomNumbersGenerator ) );

            // set new pool count
            uint8_t count = m_pools[nodeIndex].poolSequenceCounter;
            m_pools[nodeIndex].poolSequenceCounter++;
            poolHandle.SetPoolCount( count );
            QC_DEBUG( "Memory Pool Handle node type %d count %d random Number %" PRIu32
                      " pid %" PRIu32 "",
                      poolHandle.GetMemoryHandle().GetNodeType(),
                      poolHandle.GetMemoryHandle().GetRandomNumber(),
                      poolHandle.GetMemoryHandle().GetProcessId() );
            QC_DEBUG( "Pool Handle created with pool count %d random,number %" PRIu32 "",
                      poolHandle.GetPoolCount(), poolHandle.GetRandomNumber() );

            std::map<QCMemoryPoolHandle_t, std::reference_wrapper<QCMemoryPoolIfs>> &poolsMap =
                    m_pools[nodeIndex].poolMap;
            if ( QC_STATUS_OK == IS_IN_DB_STATUS( poolsMap, poolHandle ) )
            {
                QC_ERROR( "COULD NOT GENERATE UNIQUE POOL HANDLE" );
                state = QC_OBJECT_STATE_ERROR;
                status = QC_STATUS_FAIL;
            }
            else
            {
                status = QC_STATUS_NULL_PTR;
                state = QC_OBJECT_STATE_ERROR;
                QC_DEBUG( "GENERATED UNIQUE POOL HANDLE" );

                QCMemoryPoolConfig_t config( m_config.allocators[poolCfg.allocator] );
                config.buff = poolCfg.buff;
                config.maxElements = poolCfg.maxElements;
                config.name = poolCfg.name;

                // Create pool
                QCMemoryPoolIfs *pool = new Pool( config );
                // Initialize pool
                if ( nullptr != pool )
                {
                    status = pool->Init();
                    if ( status == QC_STATUS_OK )
                    {
                        state = QC_OBJECT_STATE_READY;
                        poolsMap.insert( { poolHandle, std::ref( *pool ) } );
                        status = IS_IN_DB_STATUS( poolsMap, poolHandle );
                        QC_DEBUG( "poolsMap,size() == %d", poolsMap.size() );
                        QC_DEBUG( "GetState () == %lu", state );
                    }
                    else if ( status == QC_STATUS_FAIL )
                    {
                        QC_ERROR( "status = pool->Init() returned QC_STATUS_FAIL" );
                    }
                    else
                    {
                        state = QC_OBJECT_STATE_READY;
                        QC_ERROR( "GetState () == %d", state );
                        pool->~QCMemoryPoolIfs();
                        QC_ERROR( "Pool Creation & Initialization or Inseretion to DB failed - "
                                  "destroying" );
                    }
                }
            }
        }
    }

    QC_DEBUG( "GetState () == %d", status );
    SetState( state );
    return status;
}

QCStatus_e ManagerLocal::DestroyPool( const QCMemoryPoolHandle_t &poolHandle )
{
    QCStatus_e status = QC_STATUS_OK;
    // check Memory Handle correctness
    uint8_t nodeIndex;
    const QCMemoryHandle_t &memoryHandle = poolHandle.GetMemoryHandle();

    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( GetState() != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", GetState() );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( false == IsMemoryHandleRegistered( memoryHandle, nodeIndex ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Memory Handle node type %d count %d random Number %d pid %d",
                  memoryHandle.GetNodeType(), memoryHandle.GetRandomNumber(),
                  memoryHandle.GetProcessId() );
    }
    else
    {
        readLock.unlock();

        // Use the individual pool's mutex for thread-safe access
        std::unique_lock<std::shared_mutex> poolLock( m_pools[nodeIndex].poolMutex );

        if ( m_pools[nodeIndex].poolMap.find( poolHandle ) == m_pools[nodeIndex].poolMap.end() )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Memory Pool Handle not found node type %d count %d random Number %" PRIu32
                      " pid %" PRIu32 "",
                      poolHandle.GetMemoryHandle().GetNodeType(),
                      poolHandle.GetMemoryHandle().GetRandomNumber(),
                      poolHandle.GetMemoryHandle().GetProcessId() );
            QC_ERROR( "Pool Handle created with pool count %d random,number %" PRIu32 "",
                      poolHandle.GetPoolCount(), poolHandle.GetRandomNumber() );
        }
        else
        {
            QCBufferPropBase_t buff;
            std::map<QCMemoryPoolHandle_t, std::reference_wrapper<QCMemoryPoolIfs>> &poolsMap =
                    m_pools[nodeIndex].poolMap;
            auto it = poolsMap.find( poolHandle );
            QC_INFO( "Destroying pool with elements count %d size %d",
                     it->second.get().GetConfiguration().maxElements,
                     it->second.get().GetConfiguration().buff.size );
            delete ( &it->second.get() );
            if ( 1 != poolsMap.erase( poolHandle ) )
            {
                status = QC_STATUS_FAIL;
            }
        }
    }

    if ( QC_STATUS_FAIL == status )
    {
        QC_ERROR( "m_state = QC_OBJECT_STATE_ERROR" );
        SetState( QC_OBJECT_STATE_ERROR );
    }

    return status;
}

QCStatus_e ManagerLocal::AllocateBufferFromPool( const QCMemoryPoolHandle_t &poolHandle,
                                                 QCBufferDescriptorBase_t &buff )
{
    QCStatus_e status = QC_STATUS_OK;
    // check Memory Handle correctness
    uint8_t nodeIndex;
    const QCMemoryHandle_t &memoryHandle = poolHandle.GetMemoryHandle();

    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( GetState() != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", GetState() );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( false == IsMemoryHandleRegistered( memoryHandle, nodeIndex ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Memory Handle not found node type %d count %d random Number %" PRIu32
                  " pid %" PRIu32 "",
                  memoryHandle.GetNodeType(), memoryHandle.GetRandomNumber(),
                  memoryHandle.GetProcessId() );
    }
    else
    {
        readLock.unlock();

        // Use the individual pool's mutex for thread-safe access
        std::shared_lock<std::shared_mutex> poolLock( m_pools[nodeIndex].poolMutex );

        if ( m_pools[nodeIndex].poolMap.find( poolHandle ) == m_pools[nodeIndex].poolMap.end() )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Memory Pool Handle not found node type %d count %d random Number %" PRIu32
                      " pid %" PRIu32 "",
                      poolHandle.GetMemoryHandle().GetNodeType(),
                      poolHandle.GetMemoryHandle().GetRandomNumber(),
                      poolHandle.GetMemoryHandle().GetProcessId() );
            QC_ERROR( "Pool Handle created with pool count %d random,number %" PRIu32 "",
                      poolHandle.GetPoolCount(), poolHandle.GetRandomNumber() );
        }
        else
        {
            auto it = m_pools[nodeIndex].poolMap.find( poolHandle );
            status = it->second.get().GetElement( buff );
        }
    }

    if ( QC_STATUS_FAIL == status )
    {
        QC_ERROR( "m_state = QC_OBJECT_STATE_ERROR" );
        SetState( QC_OBJECT_STATE_ERROR );
    }

    return status;
}

QCStatus_e ManagerLocal::PutBufferToPool( const QCMemoryPoolHandle_t &poolHandle,
                                          const QCBufferDescriptorBase_t &buff )
{
    QCStatus_e status = QC_STATUS_OK;
    // check Memory Handle correctness
    uint8_t nodeIndex;
    const QCMemoryHandle_t &memoryHandle = poolHandle.GetMemoryHandle();

    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( GetState() != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", GetState() );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( false == IsMemoryHandleRegistered( memoryHandle, nodeIndex ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Memory Handle not found node type %d count %d random Number %" PRIu32
                  " pid %" PRIu32 "",
                  memoryHandle.GetNodeType(), memoryHandle.GetRandomNumber(),
                  memoryHandle.GetProcessId() );
    }
    else
    {
        readLock.unlock();

        // Use the individual pool's mutex for thread-safe access
        std::shared_lock<std::shared_mutex> poolLock( m_pools[nodeIndex].poolMutex );

        if ( m_pools[nodeIndex].poolMap.find( poolHandle ) == m_pools[nodeIndex].poolMap.end() )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Memory Pool Handle not found node type %d count %d random Number %" PRIu32
                      " pid %" PRIu32 "",
                      poolHandle.GetMemoryHandle().GetNodeType(),
                      poolHandle.GetMemoryHandle().GetRandomNumber(),
                      poolHandle.GetMemoryHandle().GetProcessId() );
            QC_ERROR( "Pool Handle created with pool count %d random,number %" PRIu32 "",
                      poolHandle.GetPoolCount(), poolHandle.GetRandomNumber() );
        }
        else
        {
            auto it = m_pools[nodeIndex].poolMap.find( poolHandle );
            status = it->second.get().PutElement( buff );
        }
    }

    if ( QC_STATUS_FAIL == status )
    {
        QC_ERROR( "m_state = QC_OBJECT_STATE_ERROR" );
        SetState( QC_OBJECT_STATE_ERROR );
    }

    return status;
}

// for constant allocations/deallocations for nodes
// should be used only in initialization / deinitialization
QCStatus_e ManagerLocal::AllocateBuffer( const QCMemoryHandle_t handle,
                                         const QCMemoryAllocator_e allocator,
                                         const QCBufferPropBase_t &request,
                                         QCBufferDescriptorBase_t &buff )
{
    QCStatus_e status = QC_STATUS_OK;
    uint8_t nodeIndex;
    QCObjectState_e state = GetState();

    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( state != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", state );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( 0 == request.size )
    {
        QC_ERROR( "BAD INPUT size=%d", request.size );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( false == IsMemoryHandleRegistered( handle, nodeIndex ) )
    {
        QC_ERROR( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 "",
                  handle.GetNodeType(), handle.GetRandomNumber(), handle.GetProcessId() );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( QC_MEMORY_ALLOCATOR_LAST <= allocator )
    {
        QC_ERROR( "QC_MEMORY_ALLOCATOR_LAST(%d) <= allocator, allocator=%d",
                  QC_MEMORY_ALLOCATOR_LAST, allocator );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        readLock.unlock();

        // Use the individual allocation's mutex for thread-safe access
        std::unique_lock<std::shared_mutex> allocLock( m_allocations[nodeIndex].allocationMutex );

        QC_DEBUG( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 "",
                  handle.GetNodeType(), handle.GetRandomNumber(), handle.GetProcessId() );
        QC_DEBUG( "Allocating buffer using allocator %d ", allocator );
        QCMemoryAllocatorIfs &allocatorRef = m_config.allocators[allocator];
        QC_DEBUG( " allocator %s type %d", allocatorRef.GetConfiguration().name.c_str(),
                  allocatorRef.GetConfiguration().type );

        status = allocatorRef.Allocate( request, buff );
        if ( QC_STATUS_FAIL == status )
        {
            QC_ERROR( "QC_STATUS_FAIL == status" );
            state = QC_OBJECT_STATE_ERROR;
        }
        else if ( QC_STATUS_OK == status )
        {
            std::set<QCBufferDescriptorBase_t> &bufferSet = m_allocations[nodeIndex].allocationSet;
            bufferSet.insert( buff );
            status = IS_IN_DB_STATUS( bufferSet, buff );
            if ( QC_STATUS_OK != status )
            {
                state = QC_OBJECT_STATE_ERROR;
                QC_ERROR( "GetState () == %d", state );
                // Attempt to free bufferwhich info wasnt properly inserted into
                // data base
                allocatorRef.Free( buff );
            }
        }
        else
        {
        }
    }

    QC_DEBUG( "GetState () == %d", status );
    SetState( state );

    return status;
}

QCStatus_e ManagerLocal::FreeBuffer( const QCMemoryHandle_t handle,
                                     const QCBufferDescriptorBase_t &buff )
{
    QCStatus_e status = QC_STATUS_OK;
    uint8_t nodeIndex;

    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    if ( GetState() != QC_OBJECT_STATE_READY )
    {
        QC_ERROR( "GetState () != QC_OBJECT_STATE_READY, state =%d", GetState() );
        status = QC_STATUS_BAD_STATE;
    }
    // validate handle
    else if ( false == IsMemoryHandleRegistered( handle, nodeIndex ) )
    {
        QC_ERROR( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 "",
                  handle.GetNodeType(), handle.GetRandomNumber(), handle.GetProcessId() );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( QC_MEMORY_ALLOCATOR_LAST <= buff.allocatorType )
    {
        QC_ERROR( "QC_MEMORY_ALLOCATOR_LAST(%d) <= allocator, allocator=%d",
                  QC_MEMORY_ALLOCATOR_LAST, buff.allocatorType );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        readLock.unlock();

        // Use the individual allocation's mutex for thread-safe access
        std::unique_lock<std::shared_mutex> allocLock( m_allocations[nodeIndex].allocationMutex );

        // Validate existance of the buffer pointer in the data base
        std::set<QCBufferDescriptorBase_t> &bufferSet = m_allocations[nodeIndex].allocationSet;
        if ( QC_STATUS_OK == IS_IN_DB_STATUS( bufferSet, buff ) )
        {
            QCMemoryAllocatorIfs &allocatorRef = m_config.allocators[buff.allocatorType];
            QC_DEBUG( "allocatorRef name %s", allocatorRef.GetConfiguration().name.c_str() );
            status = allocatorRef.Free( buff );
            if ( QC_STATUS_OK == status )
            {
                bufferSet.erase( buff );
                status = IS_NOT_IN_DB_STATUS( bufferSet, buff );
            }
        }
        else
        {
            QC_ERROR( "Descriptor not in DB" );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_FAIL == status )
    {
        QC_ERROR( "m_state = QC_OBJECT_STATE_ERROR" );
        SetState( QC_OBJECT_STATE_ERROR );
    }

    return status;
}

//"Garbage collector"
QCStatus_e ManagerLocal::ReclaimResources( const QCMemoryHandle_t &handle )
{
    QCStatus_e status = QC_STATUS_OK;
    QCObjectState_e state = GetState();

    if ( state != QC_OBJECT_STATE_READY )
    {
        if ( state != QC_OBJECT_STATE_DEINITIALIZING )
        {
            QC_ERROR( "state != QC_OBJECT_STATE_READY" );
            QC_ERROR( "GetState () == %d", state );
        }
        else
        {
            QC_DEBUG( "state == QC_OBJECT_STATE_DEINITIALIZING" );
        }
        // changing temporally object state to allow call to
        // memory release methods which are blocked by wrong state
        SetState( QC_OBJECT_STATE_READY );
    }

    uint8_t nodeIndex;
    // scoped lock for handle lookup
    std::shared_lock<std::shared_mutex> readLock( m_handle2NodeIdLock );
    // validate handle
    if ( false == IsMemoryHandleRegistered( handle, nodeIndex ) )
    {
        QC_ERROR( "handle elegal" );
        QC_ERROR( "Memory Handle node type %d count %d random Number %" PRIu32 " pid %" PRIu32 "",
                  handle.GetNodeType(), handle.GetRandomNumber(), handle.GetProcessId() );
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        readLock.unlock();

        // reclaim stand alone allocations
        // ###############################
        // create copy of buffers map for a given handle,
        // use of copy instead of reference required to cope with potential
        // database failure as part of FreeBuffer() call
        // or attempt to allocated buffers for the same handle during resources reclaim
        std::set<QCBufferDescriptorBase_t> bufferMap;
        {
            // scoped lock to copy the buffer map for a specific client/node
            std::unique_lock<std::shared_mutex> allocLock(
                    m_allocations[nodeIndex].allocationMutex );
            bufferMap = m_allocations[nodeIndex].allocationSet;
        }
        QC_DEBUG( "bufferMap.size() %d ", bufferMap.size() );

        if ( bufferMap.empty() )
        {
            QC_DEBUG( "No stand alone allocations " );
        }
        else
        {
            // itterate over map and release allocations
            uint32_t freedBuffersCount = 0;
            std::set<QCBufferDescriptorBase_t>::iterator it = bufferMap.begin();
            for ( ; it != bufferMap.end(); )
            {
                QC_DEBUG( "freedBuffersCount %d ", freedBuffersCount );

                QCBufferDescriptorBase_t buffDescriptor = *it;
                QC_DEBUG( "buffDescriptor.pBuf %p buffDescriptor.allocatorType %d ",
                          buffDescriptor.pBuf, buffDescriptor.allocatorType );

                QCStatus_e localStatus = FreeBuffer( handle, buffDescriptor );
                if ( QC_STATUS_OK != localStatus )
                {
                    QC_ERROR( "allocatorPtr->Free(QCBufferDescriptorBase_t) returned error %d",
                              localStatus );
                    status = localStatus;
                    state = QC_OBJECT_STATE_ERROR;
                    QC_ERROR( "GetState () == %d", state );
                }
                else
                {
                    freedBuffersCount++;
                    QC_DEBUG( "freedBuffersCount %d", freedBuffersCount );
                }
                it = bufferMap.erase( it );   // erase returns next iterator
                QC_DEBUG( "bufferMap.size() %d ", bufferMap.size() );
            }
            QC_DEBUG( "Total freedBuffersCount %d", freedBuffersCount );
        }

        // reclaim pool allocations & destroy pools
        // ########################################
        // create copy of pool map for a given handle,
        // use of copy instead of reference required to cope with potential
        // database failure as part of DestroyPool() call
        // or attempt to allocated buffers for the same handle during resources reclaim
        // itterate over map and release allocations
        std::map<QCMemoryPoolHandle_t, std::reference_wrapper<QCMemoryPoolIfs>> poolMap;
        {
            // scoped lock to copy the pool map for a specific client/node
            std::unique_lock<std::shared_mutex> poolLock( m_pools[nodeIndex].poolMutex );
            poolMap = m_pools[nodeIndex].poolMap;
        }

        QC_DEBUG( "poolMap.size() %d ", poolMap.size() );

        if ( poolMap.empty() )
        {
            QC_DEBUG( "No Pool allocations " );
        }
        else
        {
            uint32_t freedPoolsCount = 0;
            std::map<QCMemoryPoolHandle_t, std::reference_wrapper<QCMemoryPoolIfs>>::iterator it =
                    poolMap.begin();
            for ( ; it != poolMap.end(); )
            {
                QC_DEBUG( "Destroy Pool with Memory Pool Handle node type %d count %d random "
                          "Number %" PRIu32 " pid %" PRIu32 "",
                          it->first.GetMemoryHandle().GetNodeType(),
                          it->first.GetMemoryHandle().GetRandomNumber(),
                          it->first.GetMemoryHandle().GetProcessId() );
                QC_DEBUG( "& Pool Handle pool count %d random,number %" PRIu32 "",
                          it->first.GetPoolCount(), it->first.GetRandomNumber() );
                QCStatus_e localStatus = DestroyPool( it->first );
                if ( QC_STATUS_OK != localStatus )
                {
                    status = localStatus;
                    state = QC_OBJECT_STATE_ERROR;
                    QC_ERROR( "GetState () == %d", state );
                    QC_ERROR( "Destroy Pool failed ith Memory Pool Handle node type %d count %d "
                              "random Number %" PRIu32 " pid %" PRIu32 "",
                              it->first.GetMemoryHandle().GetNodeType(),
                              it->first.GetMemoryHandle().GetRandomNumber(),
                              it->first.GetMemoryHandle().GetProcessId() );
                    QC_ERROR( "& Pool Handle pool count %d random,number %" PRIu32 "",
                              it->first.GetPoolCount(), it->first.GetRandomNumber() );
                }
                else
                {
                    freedPoolsCount++;
                }
                it = poolMap.erase( it );   // erase returns next iterator
                QC_DEBUG( "poolMap.size() %d ", poolMap.size() );
            }

            QC_DEBUG( "Total freedPoolsCount %d", freedPoolsCount );
        }
    }

    QC_DEBUG( "GetState () == %d", state );
    SetState( state );

    return status;
}

inline bool ManagerLocal::IsMemoryHandleRegistered( const QCMemoryHandle_t &handle,
                                                    uint8_t &nodeId )
{
    bool result = true;
    auto it = m_handleToNodeIdInVector.find( handle );
    if ( it == m_handleToNodeIdInVector.end() )
    {
        result = false;
    }
    else
    {
        nodeId = it->second;
    }

    return result;
}

inline bool ManagerLocal::IsNodeIdUnique( const QCNodeID_t &node )
{
    bool result = true;
    // iterate over registered node memory handles
    for ( auto i = m_handleToNodeIdInVector.begin(); i != m_handleToNodeIdInVector.end(); i++ )
    {
        // check if node Enum is equel to the new one
        if ( node.id == i->second )
        {
            result = false;
            break;
        }
    }

    return result;
}


}   // namespace Memory
}   // namespace QC
