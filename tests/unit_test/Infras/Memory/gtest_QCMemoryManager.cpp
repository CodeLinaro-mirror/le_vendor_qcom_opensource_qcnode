// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/HeapAllocator.hpp"
#include "QC/Infras/Memory/ManagerLocal.hpp"
#include "QC/Infras/Memory/Pool.hpp"
#include "MockCLib.hpp"
#include "gtest/gtest.h"


#include <atomic>   // added
#include <chrono>   // added
#include <condition_variable>
#include <semaphore.h>
#include <thread>   // added
#include <vector>   // added

using namespace QC;
using namespace QC::Memory;


class FakeAllocator : public QCMemoryAllocatorIfs
{
public:
    FakeAllocator() : QCMemoryAllocatorIfs( { "Fake Allocator" }, QC_MEMORY_ALLOCATOR_HEAP ) {}
    ~FakeAllocator() {};

    virtual QCStatus_e Allocate( const QCBufferPropBase_t &request,
                                 QCBufferDescriptorBase_t &response )
    {
        response.size = request.size;
        response.allocatorType = QC_MEMORY_ALLOCATOR_HEAP;
        return QC_STATUS_OK;
    };
    virtual QCStatus_e Free( const QCBufferDescriptorBase_t &buff ) { return QC_STATUS_FAIL; };
};

class FakeAllocator2 : public QCMemoryAllocatorIfs
{
public:
    FakeAllocator2() : QCMemoryAllocatorIfs( { "Fake Allocator" }, QC_MEMORY_ALLOCATOR_HEAP ) {}
    ~FakeAllocator2() {};

    virtual QCStatus_e Allocate( const QCBufferPropBase_t &request,
                                 QCBufferDescriptorBase_t &response )
    {
        response.size = request.size;
        response.allocatorType = QC_MEMORY_ALLOCATOR_HEAP;
        return QC_STATUS_FAIL;
    };
    virtual QCStatus_e Free( const QCBufferDescriptorBase_t &buff ) { return QC_STATUS_FAIL; };
};

class Test_QCMemorymanager : public testing::Test
{
protected:
    void SetUp() override
    {
        std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
                allocators = { allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
                               allocatorIfs1, allocatorIfs2, allocatorIfs1 };

        Ifs = reinterpret_cast<QCMemoryManagerIfs *>( &mm );
        ASSERT_NE( nullptr, Ifs );

        QCMemoryManagerInit_t memorymanagerInit( 8, allocators );

        status = Ifs->Initialize( memorymanagerInit );
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    void TearDown() override
    {
        status = Ifs->DeInitialize();
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    QCStatus_e status;
    ManagerLocal mm;
    QCMemoryManagerIfs *Ifs;
    HeapAllocator allocatorIfs1;
    HeapAllocator allocatorIfs2;
};

/**
 * Parameterized test fixture for stress and concurrent tests
 * Allows running tests with different iteration counts
 */
class Test_QCMemorymanager_Stress : public Test_QCMemorymanager,
                                    public ::testing::WithParamInterface<int>
{
};

TEST_F( Test_QCMemorymanager, SANITY_creation_and_destruction )
{
    {

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 3 };
        QCMemoryHandle_t handle1;

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        QCNodeID_t nodeExtra = { "Test node", QC_NODE_TYPE_LAST, 1 };
        QCMemoryHandle_t handleExtra;
        handleExtra.SetNodeCount( 255 );
        handleExtra.SetNodeType( (QCNodeType_e) ( QC_NODE_TYPE_LAST + 1 ) );

        status = Ifs->Register( nodeExtra, handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        nodeExtra.type = QC_NODE_TYPE_QNN;
        nodeExtra.id = 9;
        status = Ifs->Register( nodeExtra, handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        nodeExtra.id = 8;
        status = Ifs->Register( nodeExtra, handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->UnRegister( handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    {
        ManagerLocal instance;
        std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
                allocators = { allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
                               allocatorIfs1, allocatorIfs2, allocatorIfs1 };

        QCMemoryManagerInit_t mmInit( 0, allocators );

        status = instance.Initialize( mmInit );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
    }

    {
        ManagerLocal instance;

        status = instance.DeInitialize();
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );
    }

    {
        ManagerLocal *instance = new ManagerLocal();
        std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
                allocators = { allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
                               allocatorIfs1, allocatorIfs2, allocatorIfs1 };

        QCMemoryManagerInit_t mmInit( 4, allocators );

        status = instance->Initialize( mmInit );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = instance->Initialize( mmInit );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        instance->~ManagerLocal();
    }
}


TEST_F( Test_QCMemorymanager, SANITY_creation_and_registration )
{
    {

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        QCNodeID_t node2 = { "Test node2", QC_NODE_TYPE_EVA_DFS, 1 };
        QCMemoryHandle_t handle2;

        QCNodeID_t node3 = { "Test node3", QC_NODE_TYPE_FADAS_REMAP, 2 };
        QCMemoryHandle_t handle3;

        QCNodeID_t node4 = { "Test node4", QC_NODE_TYPE_CUSTOM_3, 3 };
        QCMemoryHandle_t handle4;

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        status = Ifs->Register( node2, handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_EVA_DFS, handle2.GetNodeType() );
        ASSERT_EQ( 2, handle2.GetNodeCount() );

        status = Ifs->Register( node3, handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle3.GetNodeType() );
        ASSERT_EQ( 3, handle3.GetNodeCount() );

        QCMemoryHandle_t handleDummy;
        status = Ifs->Register( node3, handleDummy );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->Register( node4, handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle4.GetNodeType() );
        ASSERT_EQ( 4, handle4.GetNodeCount() );

        QCNodeID_t nodeExtra = { "Test node4", QC_NODE_TYPE_CUSTOM_3, 3 };
        QCMemoryHandle_t handleExtra;

        status = Ifs->Register( nodeExtra, handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->UnRegister( handleExtra );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->UnRegister( handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    {
        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;
        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }
}

TEST_F( Test_QCMemorymanager, SANITY_basic_stand_alone_buffer_allocation )
{
    {

        QCBufferPropBase_t request[4];
        request[0].size = 10;
        request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[0].cache = QC_CACHEABLE;

        request[1].size = 20;
        request[1].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[1].cache = QC_CACHEABLE;

        request[2].size = 1024 * 2;
        request[2].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[2].cache = QC_CACHEABLE;

        request[3].size = 1024 * 10;
        request[3].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[3].cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t response[4];

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        QCNodeID_t node2 = { "Test node2", QC_NODE_TYPE_EVA_DFS, 1 };
        QCMemoryHandle_t handle2;

        QCNodeID_t node3 = { "Test node3", QC_NODE_TYPE_FADAS_REMAP, 2 };
        QCMemoryHandle_t handle3;

        QCNodeID_t node4 = { "Test node4", QC_NODE_TYPE_CUSTOM_3, 3 };
        QCMemoryHandle_t handle4;

        QCNodeID_t node5 = { "Test node5", QC_NODE_TYPE_FADAS_REMAP, 4 };
        QCMemoryHandle_t handle5;

        QCNodeID_t node6 = { "Test node6", QC_NODE_TYPE_EVA_DFS, 5 };
        QCMemoryHandle_t handle6;

        QCNodeID_t node7 = { "Test node7", QC_NODE_TYPE_FADAS_REMAP, 6 };
        QCMemoryHandle_t handle7;

        QCNodeID_t node8 = { "Test node8", QC_NODE_TYPE_CUSTOM_3, 7 };
        QCMemoryHandle_t handle8;

        QCNodeID_t node9 = { "Test node9", QC_NODE_TYPE_CUSTOM_3, 7 };
        QCMemoryHandle_t handle9;

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        status = Ifs->Register( node2, handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_EVA_DFS, handle2.GetNodeType() );
        ASSERT_EQ( 2, handle2.GetNodeCount() );

        status = Ifs->Register( node3, handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle3.GetNodeType() );
        ASSERT_EQ( 3, handle3.GetNodeCount() );

        status = Ifs->Register( node4, handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle4.GetNodeType() );
        ASSERT_EQ( 4, handle4.GetNodeCount() );

        status = Ifs->Register( node5, handle5 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle5.GetNodeType() );
        ASSERT_EQ( 5, handle5.GetNodeCount() );

        status = Ifs->Register( node6, handle6 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_EVA_DFS, handle6.GetNodeType() );
        ASSERT_EQ( 6, handle6.GetNodeCount() );

        status = Ifs->Register( node7, handle7 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle7.GetNodeType() );
        ASSERT_EQ( 7, handle7.GetNodeCount() );

        status = Ifs->Register( node8, handle8 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle8.GetNodeType() );
        ASSERT_EQ( 8, handle8.GetNodeCount() );

        status = Ifs->Register( node9, handle9 );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request[0], response[0] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle2, QC_MEMORY_ALLOCATOR_HEAP, request[1], response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle3, QC_MEMORY_ALLOCATOR_HEAP, request[2], response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle4, QC_MEMORY_ALLOCATOR_HEAP, request[3], response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle1, response[0] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle2, response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle3, response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle4, response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle8 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle7 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle6 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle5 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->Register( node4, handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle4.GetNodeType() );
        ASSERT_EQ( 1, handle4.GetNodeCount() );

        request[3].size = (size_t) -1;
        status = Ifs->AllocateBuffer( handle4, QC_MEMORY_ALLOCATOR_HEAP, request[3], response[3] );
        ASSERT_EQ( QC_STATUS_FAIL, status );
    }
}


TEST_F( Test_QCMemorymanager, SANITY_basic_stand_alone_buffer_allocation_2 )
{
    {
        QCBufferPropBase_t request;
        request.size = 0;
        request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request.cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t response;

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        status = Ifs->AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
        request.size = 10;

        QCMemoryHandle_t handle2;

        status = Ifs->AllocateBuffer( handle2, QC_MEMORY_ALLOCATOR_HEAP, request, response );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_LAST, request, response );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->FreeBuffer( handle2, response );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        response.allocatorType = QC_MEMORY_ALLOCATOR_LAST;
        status = Ifs->FreeBuffer( handle1, response );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }
}

TEST_F( Test_QCMemorymanager, SANITY_stand_alone_buffer_allocation )
{
    {

        QCBufferPropBase_t request[4];
        request[0].size = 10;
        request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[0].cache = QC_CACHEABLE;

        request[1].size = 20;
        request[1].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[1].cache = QC_CACHEABLE;

        request[2].size = 1024 * 2;
        request[2].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[2].cache = QC_CACHEABLE;

        request[3].size = 1024 * 10;
        request[3].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[3].cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t response[4];

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        QCNodeID_t node2 = { "Test node2", QC_NODE_TYPE_EVA_DFS, 1 };
        QCMemoryHandle_t handle2;

        QCNodeID_t node3 = { "Test node3", QC_NODE_TYPE_FADAS_REMAP, 2 };
        QCMemoryHandle_t handle3;

        QCNodeID_t node4 = { "Test node4", QC_NODE_TYPE_CUSTOM_3, 3 };
        QCMemoryHandle_t handle4;

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        status = Ifs->Register( node2, handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_EVA_DFS, handle2.GetNodeType() );
        ASSERT_EQ( 2, handle2.GetNodeCount() );

        status = Ifs->Register( node3, handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle3.GetNodeType() );
        ASSERT_EQ( 3, handle3.GetNodeCount() );

        status = Ifs->Register( node4, handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle4.GetNodeType() );
        ASSERT_EQ( 4, handle4.GetNodeCount() );

        status = Ifs->AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request[0], response[0] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle2, QC_MEMORY_ALLOCATOR_HEAP, request[1], response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle3, QC_MEMORY_ALLOCATOR_HEAP, request[2], response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle4, QC_MEMORY_ALLOCATOR_HEAP, request[3], response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        // incorrect free 1
        status = Ifs->FreeBuffer( handle1, response[3] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->FreeBuffer( handle2, response[2] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->FreeBuffer( handle3, response[1] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->FreeBuffer( handle4, response[0] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        // incorrect free 2
        QCBufferDescriptorBase_t responseTmp = response[0];
        response[0].pBuf = nullptr;
        status = Ifs->FreeBuffer( handle1, response[0] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
        response[0] = responseTmp;

        responseTmp = response[1];
        response[1].pBuf = (void *) 0xFFFFFFFFFFFFFFFF;
        status = Ifs->FreeBuffer( handle2, response[1] );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
        response[1] = responseTmp;

        // correct free
        status = Ifs->FreeBuffer( handle1, response[0] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle2, response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle3, response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle4, response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    {
        ManagerLocal instance;

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        status = instance.Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        status = instance.UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        QCBufferPropBase_t request;
        QCBufferDescriptorBase_t response;

        status = instance.AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        status = instance.FreeBuffer( handle1, response );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );
    }
}

TEST_F( Test_QCMemorymanager, SANITY_reclaim_resources )
{
    {

        QCBufferPropBase_t request[4];
        request[0].size = 10;
        request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[0].cache = QC_CACHEABLE;

        request[1].size = 20;
        request[1].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[1].cache = QC_CACHEABLE;

        request[2].size = 1024 * 2;
        request[2].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[2].cache = QC_CACHEABLE;

        request[3].size = 1024 * 10;
        request[3].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request[3].cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t response[4];

        QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
        QCMemoryHandle_t handle1;

        QCNodeID_t node2 = { "Test node2", QC_NODE_TYPE_EVA_DFS, 1 };
        QCMemoryHandle_t handle2;

        QCNodeID_t node3 = { "Test node3", QC_NODE_TYPE_FADAS_REMAP, 2 };
        QCMemoryHandle_t handle3;

        QCNodeID_t node4 = { "Test node4", QC_NODE_TYPE_CUSTOM_3, 3 };
        QCMemoryHandle_t handle4;

        status = Ifs->Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle1.GetNodeType() );
        ASSERT_EQ( 1, handle1.GetNodeCount() );

        status = Ifs->Register( node2, handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_EVA_DFS, handle2.GetNodeType() );
        ASSERT_EQ( 2, handle2.GetNodeCount() );

        status = Ifs->Register( node3, handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_FADAS_REMAP, handle3.GetNodeType() );
        ASSERT_EQ( 3, handle3.GetNodeCount() );

        status = Ifs->Register( node4, handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_EQ( QC_NODE_TYPE_CUSTOM_3, handle4.GetNodeType() );
        ASSERT_EQ( 4, handle4.GetNodeCount() );

        status = Ifs->AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request[0], response[0] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle2, QC_MEMORY_ALLOCATOR_HEAP, request[1], response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle3, QC_MEMORY_ALLOCATOR_HEAP, request[2], response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->AllocateBuffer( handle4, QC_MEMORY_ALLOCATOR_HEAP, request[3], response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        QCMemoryPoolInitConfig_t poolCfg;
        poolCfg.buff.size = 1024;
        poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
        poolCfg.maxElements = 10;
        poolCfg.name = "test pool";
        poolCfg.allocator = QC_MEMORY_ALLOCATOR_DMA;

        QCMemoryPoolHandle_t poolHandle;

        status = Ifs->CreatePool( handle1, poolCfg, poolHandle );
        ASSERT_EQ( QC_STATUS_OK, status );

        QCBufferDescriptorBase_t buff1;
        status = Ifs->AllocateBufferFromPool( poolHandle, buff1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        QCBufferDescriptorBase_t buff2;
        status = Ifs->AllocateBufferFromPool( poolHandle, buff2 );
        ASSERT_EQ( QC_STATUS_OK, status );

        QCMemoryPoolHandle_t poolHandleDummy;
        QCBufferDescriptorBase_t buffDummy;
        status = Ifs->AllocateBufferFromPool( poolHandleDummy, buffDummy );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        poolHandleDummy.SetMemoryHandle( handle1 );
        status = Ifs->AllocateBufferFromPool( poolHandleDummy, buffDummy );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        QCMemoryHandle_t handle;
        status = Ifs->ReclaimResources( handle );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

        status = Ifs->ReclaimResources( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle2, response[1] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle3, response[2] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->FreeBuffer( handle4, response[3] );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle3 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle2 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = Ifs->UnRegister( handle4 );
        ASSERT_EQ( QC_STATUS_OK, status );
    }
}

TEST_F( Test_QCMemorymanager, SANITY_pool_create_destroy )
{
    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 1024;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 10;
    poolCfg.name = "test pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_DMA;

    QCMemoryPoolHandle_t poolHandle;

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = Ifs->Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = Ifs->CreatePool( handle1, poolCfg, poolHandle );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferDescriptorBase_t buff;
    status = Ifs->AllocateBufferFromPool( poolHandle, buff );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCMemoryPoolHandle_t poolHandleDummy;
    status = Ifs->AllocateBufferFromPool( poolHandleDummy, buff );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    status = Ifs->PutBufferToPool( poolHandle, buff );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = Ifs->PutBufferToPool( poolHandleDummy, buff );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    status = Ifs->DestroyPool( poolHandle );
    ASSERT_EQ( QC_STATUS_OK, status );

    poolCfg.buff.alignment = 0;
    status = Ifs->CreatePool( handle1, poolCfg, poolHandle );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_MAX;
    status = Ifs->CreatePool( handle1, poolCfg, poolHandle );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolCfg.allocator = QC_MEMORY_ALLOCATOR_DMA;
    QCMemoryHandle_t handle1Dummy;
    QCMemoryPoolHandle_t poolHandle1Dummy;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    status = Ifs->CreatePool( handle1Dummy, poolCfg, poolHandle );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolHandle1Dummy = poolHandle;
    poolHandle1Dummy.SetMemoryHandle( handle1Dummy );
    status = Ifs->AllocateBufferFromPool( poolHandle1Dummy, buff );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolHandleDummy.SetMemoryHandle( handle1Dummy );
    status = Ifs->DestroyPool( poolHandleDummy );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolHandleDummy.SetMemoryHandle( handle1 );
    status = Ifs->DestroyPool( poolHandleDummy );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    poolHandle1Dummy = poolHandle;
    poolHandle1Dummy.SetMemoryHandle( handle1Dummy );
    status = Ifs->PutBufferToPool( poolHandle, buff );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    {
        ManagerLocal instance;

        QCMemoryPoolHandle_t poolHandle1Dummy = poolHandle;
        poolHandle1Dummy.SetMemoryHandle( handle1Dummy );
        status = instance.DestroyPool( poolHandle1Dummy );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        status = instance.CreatePool( handle1Dummy, poolCfg, poolHandle );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        poolHandle.SetMemoryHandle( handle1 );
        status = instance.AllocateBufferFromPool( poolHandle, buff );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );

        poolHandle.SetMemoryHandle( handle1Dummy );
        status = instance.PutBufferToPool( poolHandle, buff );
        ASSERT_EQ( QC_STATUS_BAD_STATE, status );
    }

    {
        FakeAllocator allocator;
        std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
                fakeAllocators = { allocator, allocator, allocator, allocator,
                                   allocator, allocator, allocator };

        QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

        ManagerLocal instance;
        status = instance.Initialize( mmInit );
        ASSERT_EQ( QC_STATUS_OK, status );

        poolCfg.maxElements = 0;

        status = instance.Register( node1, handle1 );
        ASSERT_EQ( QC_STATUS_OK, status );

        status = instance.CreatePool( handle1, poolCfg, poolHandle );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
    }
}

/*
 * Force the `new (std::nothrow) Pool(config)` allocation in CreatePool to fail and
 * verify it reports QC_STATUS_NOMEM (the live null-check) instead of dereferencing
 * a null pool or crashing. MockC_MallocCtrlSize targets exactly the sizeof(Pool)
 * allocation, so the manager's other allocations are unaffected.
 */
TEST_F( Test_QCMemorymanager, PoolAllocFailNoMem )
{
    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 1024;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 10;
    poolCfg.name = "nomem pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_DMA;

    QCMemoryPoolHandle_t poolHandle;
    QCNodeID_t node1 = { "NoMem node", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = Ifs->Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    MockC_MallocCtrlSize( sizeof( Pool ) );
    status = Ifs->CreatePool( handle1, poolCfg, poolHandle );
    MockC_MallocCtrlSize( 0 );

    ASSERT_EQ( QC_STATUS_NOMEM, status );
}

TEST_F( Test_QCMemorymanager, SANITY_pool_create_destroy_max )
{
    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 16;
    poolCfg.buff.alignment = 16;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 1;
    poolCfg.name = "test pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_DMA;

    QCMemoryPoolHandle_t poolHandle[QC_MEMORY_MAX_POOLS_PER_NODE];

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = Ifs->Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    for ( uint32_t i = 0; i < ( QC_MEMORY_MAX_POOLS_PER_NODE ); i++ )
    {
        status = Ifs->CreatePool( handle1, poolCfg, poolHandle[i] );
        ASSERT_EQ( QC_STATUS_OK, status );
    }

    QCMemoryPoolHandle_t poolHandleLast;
    status = Ifs->CreatePool( handle1, poolCfg, poolHandleLast );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, status );
}

TEST_F( Test_QCMemorymanager, SANITY_registration_fail )
{
    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_MAX, 0 };
    QCMemoryHandle_t handle1;

    status = Ifs->Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    node1.type = QC_NODE_TYPE_RESERVED;

    status = Ifs->Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
}

TEST_F( Test_QCMemorymanager, SANITY_fake_allocator )
{
    FakeAllocator allocator;
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
            fakeAllocators = { allocator, allocator, allocator, allocator,
                               allocator, allocator, allocator };

    QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

    ManagerLocal instance;
    status = instance.Initialize( mmInit );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = instance.Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferPropBase_t request;
    request.size = 20;
    QCBufferDescriptorBase_t response;

    status = instance.AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = instance.FreeBuffer( handle1, response );
    ASSERT_EQ( QC_STATUS_FAIL, status );
}

TEST_F( Test_QCMemorymanager, SANITY_fake_allocator_reclaimresources )
{
    FakeAllocator allocator;
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
            fakeAllocators = { allocator, allocator, allocator, allocator,
                               allocator, allocator, allocator };

    QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

    ManagerLocal instance;
    status = instance.Initialize( mmInit );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = instance.Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferPropBase_t request;
    request.size = 20;
    QCBufferDescriptorBase_t response;

    status = instance.AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = instance.ReclaimResources( handle1 );
    ASSERT_EQ( QC_STATUS_FAIL, status );
}

TEST_F( Test_QCMemorymanager, SANITY_fake_allocator_reclaimresources_2 )
{
    FakeAllocator allocator;
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
            fakeAllocators = { allocator, allocator, allocator, allocator,
                               allocator, allocator, allocator };

    QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

    ManagerLocal instance;
    status = instance.Initialize( mmInit );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = instance.Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferPropBase_t request;
    request.size = 20;
    QCBufferDescriptorBase_t response;

    status = instance.AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = instance.DeInitialize();
    ASSERT_EQ( QC_STATUS_FAIL, status );
}

TEST_F( Test_QCMemorymanager, SANITY_fake_allocator_reclaimresources_3 )
{
    FakeAllocator allocator;
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
            fakeAllocators = { allocator, allocator, allocator, allocator,
                               allocator, allocator, allocator };

    QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

    ManagerLocal instance;
    status = instance.Initialize( mmInit );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = instance.Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferPropBase_t request;
    request.size = 20;
    QCBufferDescriptorBase_t response;

    status = instance.AllocateBuffer( handle1, QC_MEMORY_ALLOCATOR_HEAP, request, response );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = instance.UnRegister( handle1 );
    ASSERT_EQ( QC_STATUS_FAIL, status );
}

TEST_F( Test_QCMemorymanager, SANITY_fake_allocator_create_pool )
{
    FakeAllocator2 allocator;
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
            fakeAllocators = { allocator, allocator, allocator, allocator,
                               allocator, allocator, allocator };

    QCMemoryManagerInit_t mmInit( 4, fakeAllocators );

    ManagerLocal instance;
    status = instance.Initialize( mmInit );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCNodeID_t node1 = { "Test node1", QC_NODE_TYPE_FADAS_REMAP, 0 };
    QCMemoryHandle_t handle1;

    status = instance.Register( node1, handle1 );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 16;
    poolCfg.buff.alignment = 16;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 1;
    poolCfg.name = "test pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t poolHandle;

    status = instance.CreatePool( handle1, poolCfg, poolHandle );
    ASSERT_EQ( QC_STATUS_FAIL, status );

    status = instance.UnRegister( handle1 );
    ASSERT_EQ( QC_STATUS_BAD_STATE, status );
}


// ---------------------------------------------------------------------------
// Additional coverage for ManagerLocal decisions / error paths
// ---------------------------------------------------------------------------

/**
 * @test SANITY_register_capacity_limit
 * @brief Fill the registry up to m_config.numOfNodes then verify one more
 *        registration is rejected (covers the capacity-limit decision of Register()).
 */
TEST_F( Test_QCMemorymanager, SANITY_register_capacity_limit )
{
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST> allocators =
            { allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
              allocatorIfs1, allocatorIfs2, allocatorIfs1 };

    ManagerLocal instance;
    QCMemoryManagerInit_t mmInit( 2, allocators );
    ASSERT_EQ( QC_STATUS_OK, instance.Initialize( mmInit ) );

    QCNodeID_t node0 = { "Cap node0", QC_NODE_TYPE_CUSTOM_0, 0 };
    QCNodeID_t node1 = { "Cap node1", QC_NODE_TYPE_CUSTOM_1, 1 };
    QCMemoryHandle_t h0;
    QCMemoryHandle_t h1;

    ASSERT_EQ( QC_STATUS_OK, instance.Register( node0, h0 ) );
    ASSERT_EQ( QC_STATUS_OK, instance.Register( node1, h1 ) );

    /* node.id >= numOfNodes is rejected before the capacity check */
    QCNodeID_t nodeOutOfRange = { "Cap node2", QC_NODE_TYPE_CUSTOM_2, 2 };
    QCMemoryHandle_t hOutOfRange;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, instance.Register( nodeOutOfRange, hOutOfRange ) );

    /* duplicate node id is rejected by IsNodeIdUnique() */
    QCNodeID_t nodeDup = { "Cap node0 dup", QC_NODE_TYPE_CUSTOM_3, 0 };
    QCMemoryHandle_t hDup;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, instance.Register( nodeDup, hDup ) );

    ASSERT_EQ( QC_STATUS_OK, instance.UnRegister( h1 ) );
    ASSERT_EQ( QC_STATUS_OK, instance.UnRegister( h0 ) );
    ASSERT_EQ( QC_STATUS_OK, instance.DeInitialize() );
}

/**
 * @test SANITY_register_all_node_id_unique_iterations
 * @brief Register several nodes so IsNodeIdUnique() iterates over a populated map,
 *        then attempt duplicates for every already-used id.
 */
TEST_F( Test_QCMemorymanager, SANITY_register_all_node_id_unique_iterations )
{
    std::vector<QCMemoryHandle_t> handles;

    for ( uint8_t id = 0; id < 8; ++id )
    {
        QCNodeID_t node = { "Unique node",
                            static_cast<QCNodeType_e>( QC_NODE_TYPE_CUSTOM_0 + ( id % 4 ) ), id };
        QCMemoryHandle_t h;
        ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) ) << "id=" << (int) id;
        ASSERT_EQ( id + 1, h.GetNodeCount() );
        handles.push_back( h );
    }

    /* every id in [0,8) is taken now -> all duplicates must be rejected */
    for ( uint8_t id = 0; id < 8; ++id )
    {
        QCNodeID_t dup = { "Dup node", QC_NODE_TYPE_CUSTOM_0, id };
        QCMemoryHandle_t hDup;
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->Register( dup, hDup ) ) << "id=" << (int) id;
    }

    for ( QCMemoryHandle_t &h : handles )
    {
        ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
    }
}

/**
 * @test SANITY_pool_apis_with_unregistered_memory_handle
 * @brief All pool APIs must reject a pool handle whose memory handle is not registered.
 */
TEST_F( Test_QCMemorymanager, SANITY_pool_apis_with_unregistered_memory_handle )
{
    QCMemoryHandle_t bogusHandle;
    QCMemoryPoolHandle_t bogusPool;
    bogusPool.SetMemoryHandle( bogusHandle );

    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 2;
    poolCfg.name = "bogus pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t outHandle;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->CreatePool( bogusHandle, poolCfg, outHandle ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->DestroyPool( bogusPool ) );

    QCBufferDescriptorBase_t buff;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->AllocateBufferFromPool( bogusPool, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->PutBufferToPool( bogusPool, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->ReclaimResources( bogusHandle ) );
}

/**
 * @test SANITY_create_pool_all_allocator_types
 * @brief Create one pool per supported allocator enum value and destroy it,
 *        covering the allocator-index selection path of CreatePool().
 */
TEST_F( Test_QCMemorymanager, SANITY_create_pool_all_allocator_types )
{
    QCNodeID_t node = { "Pool alloc node", QC_NODE_TYPE_CUSTOM_0, 0 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    for ( int i = 0; i < static_cast<int>( QC_MEMORY_ALLOCATOR_LAST ); ++i )
    {
        QCMemoryPoolInitConfig_t poolCfg;
        poolCfg.buff.size = 256;
        poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
        poolCfg.maxElements = 2;
        poolCfg.name = "alloc type pool";
        poolCfg.allocator = static_cast<QCMemoryAllocator_e>( i );

        QCMemoryPoolHandle_t poolHandle;
        ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle ) )
                << "allocator=" << i;

        QCBufferDescriptorBase_t buff;
        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( poolHandle, buff ) );
        ASSERT_NE( nullptr, buff.pBuf );
        ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( poolHandle, buff ) );
        ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( poolHandle ) );
    }

    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * @test SANITY_destroy_pool_twice
 * @brief Destroying the same pool twice: the second call must be rejected because
 *        the pool handle is no longer in the node's pool map.
 */
TEST_F( Test_QCMemorymanager, SANITY_destroy_pool_twice )
{
    QCNodeID_t node = { "Destroy twice node", QC_NODE_TYPE_CUSTOM_1, 1 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 2;
    poolCfg.name = "destroy twice pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t poolHandle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( poolHandle ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->DestroyPool( poolHandle ) );

    /* pool APIs on a destroyed pool must be rejected too */
    QCBufferDescriptorBase_t buff;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->AllocateBufferFromPool( poolHandle, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->PutBufferToPool( poolHandle, buff ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * @test SANITY_pool_exhaustion_then_refill
 * @brief Drain a pool to trigger the exhausted branch of GetElement, then return
 *        all elements and allocate again.
 */
TEST_F( Test_QCMemorymanager, SANITY_pool_exhaustion_then_refill )
{
    QCNodeID_t node = { "Exhaust node", QC_NODE_TYPE_CUSTOM_2, 2 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    const uint32_t maxElements = 4;
    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = maxElements;
    poolCfg.name = "exhaust pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t poolHandle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle ) );

    std::vector<QCBufferDescriptorBase_t> buffers;
    for ( uint32_t i = 0; i < maxElements; ++i )
    {
        QCBufferDescriptorBase_t buff;
        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( poolHandle, buff ) ) << "i=" << i;
        ASSERT_NE( nullptr, buff.pBuf );
        buffers.push_back( buff );
    }

    /* pool is empty now */
    QCBufferDescriptorBase_t overflow;
    ASSERT_NE( QC_STATUS_OK, Ifs->AllocateBufferFromPool( poolHandle, overflow ) );

    for ( QCBufferDescriptorBase_t &buff : buffers )
    {
        ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( poolHandle, buff ) );
    }

    /* refilled: allocation succeeds again */
    QCBufferDescriptorBase_t again;
    ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( poolHandle, again ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( poolHandle, again ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( poolHandle ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * @test SANITY_reclaim_resources_pools_and_buffers
 * @brief ReclaimResources() must release both stand-alone allocations and pools
 *        belonging to a handle (covers both non-empty iteration loops).
 */
TEST_F( Test_QCMemorymanager, SANITY_reclaim_resources_pools_and_buffers )
{
    QCNodeID_t node = { "Reclaim both node", QC_NODE_TYPE_CUSTOM_3, 3 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    /* two stand-alone allocations */
    QCBufferPropBase_t request;
    request.size = 512;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;

    QCBufferDescriptorBase_t direct1;
    QCBufferDescriptorBase_t direct2;
    ASSERT_EQ( QC_STATUS_OK,
               Ifs->AllocateBuffer( handle, QC_MEMORY_ALLOCATOR_HEAP, request, direct1 ) );
    ASSERT_EQ( QC_STATUS_OK,
               Ifs->AllocateBuffer( handle, QC_MEMORY_ALLOCATOR_HEAP, request, direct2 ) );

    /* two pools */
    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 2;
    poolCfg.name = "reclaim pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t poolHandle1;
    QCMemoryPoolHandle_t poolHandle2;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle1 ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle2 ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->ReclaimResources( handle ) );

    /* everything is gone: buffers and pools are no longer known */
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->FreeBuffer( handle, direct1 ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->FreeBuffer( handle, direct2 ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->DestroyPool( poolHandle1 ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->DestroyPool( poolHandle2 ) );

    /* reclaiming again with nothing left is still OK (covers empty branches) */
    ASSERT_EQ( QC_STATUS_OK, Ifs->ReclaimResources( handle ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * @test SANITY_allocate_buffer_all_allocator_types
 * @brief AllocateBuffer()/FreeBuffer() for every allocator enum value.
 */
TEST_F( Test_QCMemorymanager, SANITY_allocate_buffer_all_allocator_types )
{
    QCNodeID_t node = { "Alloc types node", QC_NODE_TYPE_CUSTOM_0, 0 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    for ( int i = 0; i < static_cast<int>( QC_MEMORY_ALLOCATOR_LAST ); ++i )
    {
        QCBufferPropBase_t request;
        request.size = 1024;
        request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request.cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t response;
        ASSERT_EQ( QC_STATUS_OK,
                   Ifs->AllocateBuffer( handle, static_cast<QCMemoryAllocator_e>( i ), request,
                                        response ) )
                << "allocator=" << i;
        ASSERT_NE( nullptr, response.pBuf );
        ASSERT_EQ( QC_STATUS_OK, Ifs->FreeBuffer( handle, response ) );
    }

    /* out of range allocator enum values */
    QCBufferPropBase_t request;
    request.size = 1024;
    QCBufferDescriptorBase_t response;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               Ifs->AllocateBuffer( handle, QC_MEMORY_ALLOCATOR_LAST, request, response ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               Ifs->AllocateBuffer( handle, QC_MEMORY_ALLOCATOR_MAX, request, response ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * @test SANITY_all_apis_rejected_before_initialize
 * @brief Every public API must report QC_STATUS_BAD_STATE on a non-initialized manager.
 */
TEST_F( Test_QCMemorymanager, SANITY_all_apis_rejected_before_initialize )
{
    ManagerLocal instance;

    QCNodeID_t node = { "Bad state node", QC_NODE_TYPE_CUSTOM_0, 0 };
    QCMemoryHandle_t handle;
    QCMemoryPoolHandle_t poolHandle;
    QCBufferPropBase_t request;
    request.size = 128;
    QCBufferDescriptorBase_t buff;

    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 2;
    poolCfg.name = "bad state pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.Register( node, handle ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.UnRegister( handle ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.CreatePool( handle, poolCfg, poolHandle ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.DestroyPool( poolHandle ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.AllocateBufferFromPool( poolHandle, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.PutBufferToPool( poolHandle, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE,
               instance.AllocateBuffer( handle, QC_MEMORY_ALLOCATOR_HEAP, request, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.FreeBuffer( handle, buff ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.DeInitialize() );
}

/**
 * @test SANITY_double_deinitialize
 * @brief The second DeInitialize() must report QC_STATUS_BAD_STATE.
 */
TEST_F( Test_QCMemorymanager, SANITY_double_deinitialize )
{
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST> allocators =
            { allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
              allocatorIfs1, allocatorIfs2, allocatorIfs1 };

    ManagerLocal instance;
    QCMemoryManagerInit_t mmInit( 4, allocators );
    ASSERT_EQ( QC_STATUS_OK, instance.Initialize( mmInit ) );
    ASSERT_EQ( QC_STATUS_OK, instance.DeInitialize() );
    ASSERT_EQ( QC_STATUS_BAD_STATE, instance.DeInitialize() );

    /* re-initialize after deinit is allowed again */
    ASSERT_EQ( QC_STATUS_OK, instance.Initialize( mmInit ) );
    ASSERT_EQ( QC_STATUS_OK, instance.DeInitialize() );
}

/**
 * @test SANITY_put_buffer_to_pool_foreign_buffer
 * @brief Returning a buffer that does not belong to the pool must be rejected.
 */
TEST_F( Test_QCMemorymanager, SANITY_put_buffer_to_pool_foreign_buffer )
{
    QCNodeID_t node = { "Foreign buff node", QC_NODE_TYPE_CUSTOM_1, 1 };
    QCMemoryHandle_t handle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, handle ) );

    QCMemoryPoolInitConfig_t poolCfg;
    poolCfg.buff.size = 128;
    poolCfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    poolCfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    poolCfg.maxElements = 2;
    poolCfg.name = "foreign buff pool";
    poolCfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

    QCMemoryPoolHandle_t poolHandle;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( handle, poolCfg, poolHandle ) );

    QCBufferDescriptorBase_t foreign;
    foreign.pBuf = reinterpret_cast<void *>( 0xDEADBEEF );
    foreign.size = 128;
    foreign.allocatorType = QC_MEMORY_ALLOCATOR_HEAP;
    ASSERT_NE( QC_STATUS_OK, Ifs->PutBufferToPool( poolHandle, foreign ) );

    ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( poolHandle ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( handle ) );
}

/**
 * Test: ST_RegisterUnregister_Loop_1000000
 * Use Case: UC‑001 Register Node (Stress)
 * Scenario: ST‑02 — Register and unregister nodes repeatedly in a loop (resource churn)
 *
 * Purpose:
 *  - Stress the ManagerLocal Register/UnRegister path with 1,000,000 iterations.
 *  - Detect leaks, stale handles, and incorrect state transitions under heavy churn.
 *
 * Execution:
 *  - Each iteration builds a QCNodeID ("ChurnNode") with rotating type (CUSTOM_0..CUSTOM_3) and
 * unique id=i.
 *  - Calls Ifs->Register(node, h) and immediately Ifs->UnRegister(h), both must return
 * QC_STATUS_OK.
 *  - Every 1000th iteration, performs a second UnRegister(h) on the same handle; expects
 * QC_STATUS_BAD_ARGUMENTS.
 *
 * Expected Behavior:
 *  - All register/unregister pairs succeed (QC_STATUS_OK).
 *  - Double unregistration is rejected safely (QC_STATUS_BAD_ARGUMENTS).
 *  - No crashes, no resource leaks, manager remains in READY state.
 *
 * Notes:
 *  - Single-threaded stress; for concurrency coverage, see ST_Concurrent_* tests.
 *  - High iteration count can be time-consuming on debug builds; tune if needed.
 */

TEST_P( Test_QCMemorymanager_Stress, ST_RegisterUnregister_Loop )
{
    const int iters = GetParam();
    std::cout << "Running with " << iters << " iterations" << std::endl;

    for ( int i = 0; i < iters; ++i )
    {
        if ( ( i % 10000 ) == 0 )
        {
            printf( "ST_RegisterUnregister_Loop %d iters %d\n", i, iters );
        }

        QCNodeID_t node = { "ChurnNode", (QCNodeType_e) ( QC_NODE_TYPE_FADAS_REMAP + ( i % 4 ) ),
                            (uint8_t) ( i % 4 ) };
        QCMemoryHandle_t h;

        ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );
        ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );

        // Optional safety: double UnRegister should fail safely
        if ( ( i % 1000 ) == 0 )
        {
            EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->UnRegister( h ) );
        }
    }
}


/**
 * Test: ST_Stability_Register_Alloc_Free_Unregister_Loop
 * Flow: Register → AllocateBuffer(HEAP) → FreeBuffer(HEAP) → UnRegister
 * Goal: Stability under repeated usage (no leaks, no stale handles)
 */
TEST_P( Test_QCMemorymanager_Stress, ST_Stability_Register_Alloc_Free_Unregister_Loop )
{
    const int iters = GetParam();
    std::cout << "Running with " << iters << " iterations" << std::endl;

    for ( int i = 0; i < iters; ++i )
    {
        if ( ( i % 10000 ) == 0 )
        {
            printf( "ST_Stability_Register_Alloc_Free_Unregister_Loop %d iters %d\n", i, iters );
        }

        // 1) Register a node
        QCNodeID_t node = { "StabilityNode", QC_NODE_TYPE_CUSTOM_0, (uint8_t) ( i % 4 ) };
        QCMemoryHandle_t h;
        ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );

        // 2) Allocate via HEAP allocator
        QCBufferPropBase_t req{};
        req.size = 4096;
        req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        req.cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t resp{};
        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, resp ) );
        ASSERT_NE( nullptr, resp.pBuf );
        ASSERT_EQ( QC_MEMORY_ALLOCATOR_HEAP, resp.allocatorType );
        // Alignment check
        ASSERT_EQ( reinterpret_cast<uint64_t>( resp.pBuf ),
                   reinterpret_cast<uint64_t>( resp.pBuf ) & ~( req.alignment - 1 ) );

        // 3) Free the buffer
        ASSERT_EQ( QC_STATUS_OK, Ifs->FreeBuffer( h, resp ) );

        // Optional safety: double free should fail safely
        if ( ( i % 1000 ) == 0 )
        {
            EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->FreeBuffer( h, resp ) );
        }

        // 4) Unregister the node
        ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );

        // Optional safety: double unregister should fail safely
        if ( ( i % 1000 ) == 0 )
        {
            EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->UnRegister( h ) );
        }
    }
}

/**
 * Test: ST_Lifecycle_Churn_Register_Alloc_CreatePool_AllocFromPool_Reclaim_Free_Unregister
 * Flow per iteration:
 *   Register → AllocateBuffer(HEAP) → CreatePool → AllocateBufferFromPool → ReclaimResources →
 * FreeBuffer → UnRegister Goal: Lifecycle stability under repeated usage (no leaks, no stale
 * handles)
 */
TEST_P( Test_QCMemorymanager_Stress,
        ST_Lifecycle_Churn_Register_Alloc_CreatePool_AllocFromPool_Reclaim_Free_Unregister )
{
    const int iters = GetParam();
    std::cout << "Running with " << iters << " iterations" << std::endl;

    for ( int i = 0; i < iters; ++i )
    {
        if ( ( i % 10000 ) == 0 )
        {
            printf( "ST_Lifecycle_Churn_Register_Alloc_CreatePool_AllocFromPool_Reclaim_Free_"
                    "Unregister %d iters %d\n",
                    i, iters );
        }

        // 1) Register a node
        QCNodeID_t node = { "LifecycleChurn", QC_NODE_TYPE_CUSTOM_0, (uint8_t) ( i % 4 ) };
        QCMemoryHandle_t h;
        ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );

        // 2) Direct allocate via HEAP
        QCBufferPropBase_t req{};
        req.size = 2048;
        req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        req.cache = QC_CACHEABLE;

        QCBufferDescriptorBase_t direct{};
        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, direct ) );
        ASSERT_NE( nullptr, direct.pBuf );

        // 3) Create a small pool on the same node
        QCMemoryPoolInitConfig_t cfg;
        cfg.buff.size = 1024;
        cfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        cfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
        cfg.maxElements = 8;
        cfg.name = "churn-pool";
        cfg.allocator = QC_MEMORY_ALLOCATOR_HEAP;

        QCMemoryPoolHandle_t ph;
        ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( h, cfg, ph ) );

        // 4) Allocate one buffer from pool
        QCBufferDescriptorBase_t poolBuf{};
        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( ph, poolBuf ) );
        ASSERT_NE( nullptr, poolBuf.pBuf );
        ASSERT_NE( nullptr, poolBuf.pBuf );
        ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( ph, poolBuf ) );

        // 7) Destroy pool
        ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( ph ) );

        // 5) Reclaim all resources owned by the node
        ASSERT_EQ( QC_STATUS_OK, Ifs->ReclaimResources( h ) );

        // 6) Free the direct buffer (may already be reclaimed); accept OK or BAD_ARGUMENTS
        QCStatus_e stFree = Ifs->FreeBuffer( h, direct );
        EXPECT_TRUE( stFree == QC_STATUS_OK || stFree == QC_STATUS_BAD_ARGUMENTS );

        // 7) Unregister the node (manager should cleanup pools/buffers)
        ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );

        // Optional safety checks every 1000 iterations
        if ( ( i % 1000 ) == 0 )
        {
            EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
                       Ifs->UnRegister( h ) );   // double unregister rejected
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}


TEST_P( Test_QCMemorymanager_Stress, ST_AllocFromPool_PutBuffertoPool )
{
    const int iters = GetParam();
    std::cout << "Running with " << iters << " iterations" << std::endl;

    // 1) Register a node
    QCNodeID_t node = { "LifecycleChurn", QC_NODE_TYPE_CUSTOM_0, (uint8_t) ( 4 ) };
    QCMemoryHandle_t h;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );

    // 2) Direct allocate via HEAP
    QCBufferPropBase_t req{};
    req.size = 2048;
    req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    req.cache = QC_CACHEABLE;

    QCBufferDescriptorBase_t direct{};
    ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, direct ) );
    ASSERT_NE( nullptr, direct.pBuf );

    // 3) Create a small pool on the same node
    QCMemoryPoolInitConfig_t cfg;
    cfg.buff.size = 1024;
    cfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    cfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    cfg.maxElements = 8;
    cfg.name = "churn-pool";
    cfg.allocator = QC_MEMORY_ALLOCATOR_DMA;
    QCMemoryPoolHandle_t ph;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( h, cfg, ph ) );

    // 4) Allocate one buffer from pool
    QCBufferDescriptorBase_t poolBuf{};
    for ( int i = 0; i < iters; ++i )
    {
        if ( ( i % 10000 ) == 0 )
        {
            printf( "ST_AllocFromPool_PutBuffertoPool %d iters %d\n", i, iters );
        }

        ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( ph, poolBuf ) );
        ASSERT_NE( nullptr, poolBuf.pBuf );
        ASSERT_NE( nullptr, poolBuf.pBuf );
        ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( ph, poolBuf ) );
    }

    // 7) Destroy pool
    ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( ph ) );

    // 5) Reclaim all resources owned by the node
    ASSERT_EQ( QC_STATUS_OK, Ifs->ReclaimResources( h ) );

    // 6) Free the direct buffer (may already be reclaimed); accept OK or BAD_ARGUMENTS
    QCStatus_e stFree = Ifs->FreeBuffer( h, direct );
    EXPECT_TRUE( stFree == QC_STATUS_OK || stFree == QC_STATUS_BAD_ARGUMENTS );

    // 7) Unregister the node (manager should cleanup pools/buffers)
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
}


/**
 * Test: Concurrent_Register_CreatePool_DestroyPool_AllocateFromPool_PutBuffer_8Threads
 *
 * Scenario:
 *  - 4 threads, each with a GLOBALLY UNIQUE thread ID
 *  - Each thread registers nodes with:
 *    • Node Type: Rotates through QC_NODE_TYPE_CUSTOM_0..3 (4 types) based on iteration
 *    • Node ID: UNIQUE per thread (no wrapping) - uses atomic counter
 *  - Ensures: No duplicate node.id values across all threads
 *
 * Flow per iteration:
 *   Register → AllocateBuffer(HEAP) → CreatePool → AllocateBufferFromPool →
 *   PutBufferToPool → DestroyPool → ReclaimResources → FreeBuffer → UnRegister
 *
 * Goal:
 *  - Concurrency stability (no leaks, no stale handles, no deadlocks) under mixed API usage
 *  - Validate proper node identity management under concurrent operations
 *  - Ensure no duplicate node.id values are used
 */
TEST_P( Test_QCMemorymanager_Stress,
        Concurrent_Register_CreatePool_DestroyPool_AllocateFromPool_PutBuffer_8Threads )
{
    const int threads = 8;
    const int itersPerThread = GetParam();
    std::cout << "Running with " << itersPerThread << " iterations per thread" << std::endl;
    std::atomic<bool> start{ false };

    // Shared pool of available node IDs [0, numOfNodes-1].
    // Each thread leases an ID before Register and returns it after UnRegister,
    // so no two concurrent registrations ever share the same nodeId.
    // If the pool is empty a thread blocks on the condition variable until
    // another thread releases an ID back.
    std::vector<uint8_t> idPool = { 0, 1, 2, 3, 4, 5, 6, 7 };
    std::mutex idPoolMutex;
    std::condition_variable idPoolCV;

    auto acquireId = [&]() -> uint8_t {
        std::unique_lock<std::mutex> lock( idPoolMutex );
        // Block (release the mutex) until at least one ID is available.
        idPoolCV.wait( lock, [&] { return !idPool.empty(); } );
        uint8_t id = idPool.back();
        idPool.pop_back();
        return id;
    };
    auto releaseId = [&]( uint8_t id ) {
        {
            std::lock_guard<std::mutex> lock( idPoolMutex );
            idPool.push_back( id );
        }
        // Wake one waiting thread so it can pick up the returned ID.
        idPoolCV.notify_one();
    };

    auto worker = [&]( int tid ) {
        while ( !start.load() ) std::this_thread::yield();

        for ( int i = 0; i < itersPerThread; ++i )
        {
            // Lease a unique nodeId from the shared pool.
            // The mutex guarantees no two threads hold the same ID simultaneously.
            // The ID is released back to the pool after UnRegister, making it
            // available for reuse in subsequent iterations across any thread.
            uint8_t nodeId = acquireId();

            // printf( "test %d times with nodeId: %u\n", i, nodeId );

            // Rotate through node types based on iteration
            QCNodeType_e nodeType = static_cast<QCNodeType_e>( QC_NODE_TYPE_CUSTOM_0 + ( i % 4 ) );

            // Create unique node name per thread and iteration
            char nodeName[64];
            snprintf( nodeName, sizeof( nodeName ), "Thread%d_Iter%d_Lifecycle", tid, i );

            // 1) Register a unique node
            QCNodeID_t node = { nodeName, nodeType, nodeId };
            QCMemoryHandle_t h;
            QCStatus_e status = Ifs->Register( node, h );
            ASSERT_TRUE( QC_STATUS_OK == status || QC_STATUS_BAD_STATE == status )
                    << "Thread " << tid << " iteration " << i
                    << " failed to register node (type=" << nodeType << ", id=" << (int) nodeId
                    << ")";

            // Verify handle properties
            ASSERT_EQ( nodeType, h.GetNodeType() );
            ASSERT_GE( h.GetNodeCount(), 1 );
            // 2) Direct allocate via HEAP
            QCBufferPropBase_t req{};
            req.size = 2048;
            req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            req.cache = QC_CACHEABLE;
            QCBufferDescriptorBase_t direct{};
            status = Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, direct );
            ASSERT_TRUE( QC_STATUS_OK == status );
            if ( status == QC_STATUS_OK )
            {
                ASSERT_NE( nullptr, direct.pBuf );
            }
            // 3) Create a small pool on the same node
            QCMemoryPoolInitConfig_t cfg;
            cfg.buff.size = 1024;
            cfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            cfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
            cfg.maxElements = 8;
            cfg.name = "churn-pool";
            cfg.allocator = QC_MEMORY_ALLOCATOR_DMA;
            QCMemoryPoolHandle_t ph;
            status = Ifs->CreatePool( h, cfg, ph );
            ASSERT_TRUE( QC_STATUS_OK == status );
            // 4) Allocate one buffer from pool
            QCBufferDescriptorBase_t poolBuf{};
            status = Ifs->AllocateBufferFromPool( ph, poolBuf );
            if ( status == QC_STATUS_OK )
            {
                ASSERT_NE( nullptr, poolBuf.pBuf );
            }
            // 5) Return buffer to pool
            status = Ifs->PutBufferToPool( ph, poolBuf );
            ASSERT_TRUE( QC_STATUS_OK == status );
            // 6) Destroy pool
            status = Ifs->DestroyPool( ph );
            ASSERT_TRUE( QC_STATUS_OK == status );
            // 7) Reclaim all resources owned by the node
            status = Ifs->ReclaimResources( h );
            EXPECT_TRUE( status == QC_STATUS_OK );
            // 8) Free the direct buffer (may already be reclaimed); accept OK or BAD_ARGUMENTS
            status = Ifs->FreeBuffer( h, direct );
            EXPECT_TRUE( status == QC_STATUS_BAD_ARGUMENTS );
            // 9) Unregister the node (manager should cleanup pools/buffers)
            status = Ifs->UnRegister( h );
            EXPECT_TRUE( status == QC_STATUS_OK );
            // Return the leased nodeId back to the pool for reuse by other threads/iterations.
            releaseId( nodeId );
            // Optional safety checks every 1000 iterationss
            if ( ( i % 1000 ) == 0 )
            {
                status = Ifs->UnRegister( h );
                EXPECT_TRUE( status == QC_STATUS_BAD_ARGUMENTS || status == QC_STATUS_BAD_STATE )
                        << "Double unregister should fail for thread " << tid;
            }
            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_Register_CreatePool_DestroyPool_AllocateFromPool_PutBuffer_"
                        "8Threads %d itersPerThread %d\n",
                        i, itersPerThread );
            }
        }
    };
    std::vector<std::thread> ts;
    ts.reserve( threads );
    for ( int t = 0; t < threads; ++t )
    {
        ts.emplace_back( worker, t );
    }
    start.store( true );
    for ( auto &th : ts ) th.join();
}

/*concurrent Register/Unregister Races*/

/**
 * Test: Concurrent_Register_Unregister_UniqueNodes_8Threads
 *
 * Scenario:
 *  - 8 threads, each with a unique thread ID (0-7)
 *  - Each thread registers nodes with:
 *    • Node Type: Rotates through QC_NODE_TYPE_CUSTOM_0..3 (4 types)
 *    • Node ID: Maps to thread ID % 4 (values 0-3)
 *  - Ensures: One node type can have multiple IDs, but one ID is unique per type
 *
 * Example mapping:
 *  Thread 0: (CUSTOM_0, ID=0), (CUSTOM_1, ID=0), (CUSTOM_2, ID=0), (CUSTOM_3, ID=0)
 *  Thread 1: (CUSTOM_0, ID=1), (CUSTOM_1, ID=1), (CUSTOM_2, ID=1), (CUSTOM_3, ID=1)
 *  Thread 2: (CUSTOM_0, ID=2), (CUSTOM_1, ID=2), (CUSTOM_2, ID=2), (CUSTOM_3, ID=2)
 *  Thread 3: (CUSTOM_0, ID=3), (CUSTOM_1, ID=3), (CUSTOM_2, ID=3), (CUSTOM_3, ID=3)
 *  Thread 4: (CUSTOM_0, ID=0), ... (wraps around, but different iteration)
 *
 * Goal:
 *  - Validate concurrent registration of distinct nodes
 *  - Ensure no duplicate (type, id) pairs are registered simultaneously
 *  - Detect race conditions in node handle map
 */
TEST_P( Test_QCMemorymanager_Stress, Concurrent_Register_Unregister_UniqueNodes_4Threads )
{
    const int threads = 4;
    const int itersPerThread = GetParam();
    std::cout << "Running with " << itersPerThread << " iterations per thread" << std::endl;
    std::atomic<bool> start{ false };
    std::atomic<int> totalRegistrations{ 0 };
    auto worker = [&]( int tid ) {
        while ( !start.load() ) std::this_thread::yield();
        for ( int i = 0; i < itersPerThread; ++i )
        {
            // Each thread gets a unique node ID based on thread ID
            uint8_t nodeId = static_cast<uint8_t>( tid % 4 );   // IDs: 0, 1, 2, 3
            // Rotate through node types based on iteration
            QCNodeType_e nodeType = static_cast<QCNodeType_e>( QC_NODE_TYPE_CUSTOM_0 + ( i % 4 ) );
            // Create unique node name per thread
            char nodeName[32];
            snprintf( nodeName, sizeof( nodeName ), "Thread%d_Node", tid );
            QCNodeID_t node = { nodeName, nodeType, nodeId };
            QCMemoryHandle_t h;
            // Register the node
            QCStatus_e regStatus = Ifs->Register( node, h );
            ASSERT_EQ( QC_STATUS_OK, regStatus )
                    << "Thread " << tid << " failed to register node (type=" << nodeType
                    << ", id=" << (int) nodeId << ")";
            totalRegistrations.fetch_add( 1 );
            // Verify handle properties
            ASSERT_EQ( nodeType, h.GetNodeType() );
            ASSERT_GE( h.GetNodeCount(), 1 );
            // Small delay to increase race window
            std::this_thread::yield();
            // Unregister the node
            QCStatus_e unregStatus = Ifs->UnRegister( h );
            ASSERT_EQ( QC_STATUS_OK, unregStatus )
                    << "Thread " << tid << " failed to unregister node";
            totalRegistrations.fetch_sub( 1 );
            // Optional: Every 1000 iterations, verify double-unregister fails
            if ( ( i % 1000 ) == 0 )
            {
                EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, Ifs->UnRegister( h ) )
                        << "Double unregister should fail for thread " << tid;
            }

            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_Register_Unregister_UniqueNodes_4Threads %d itersPerThread "
                        "%d\n",
                        i, itersPerThread );
            }
        }
    };
    std::vector<std::thread> ts;
    ts.reserve( threads );
    for ( int t = 0; t < threads; ++t )
    {
        ts.emplace_back( worker, t );
    }
    start.store( true );
    for ( auto &th : ts ) th.join();
    // Verify all registrations were cleaned up
    EXPECT_EQ( 0, totalRegistrations.load() )
            << "Memory leak detected: not all nodes were unregistered";
}
/*Concurrent pool operations on the same Node*/
TEST_P( Test_QCMemorymanager_Stress, Concurrent_Pool_Operations_SameNode_8Threads )
{
    QCNodeID_t node = { "PoolNode", QC_NODE_TYPE_CUSTOM_1, 1 };
    QCMemoryHandle_t h;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );
    const int threads = 8;
    const int itersPerThread = GetParam();
    std::cout << "Running with " << itersPerThread << " iterations per thread" << std::endl;
    std::atomic<bool> start{ false };
    auto worker = [&]( int tid ) {
        while ( !start.load() ) std::this_thread::yield();
        for ( int i = 0; i < itersPerThread; ++i )
        {
            QCMemoryPoolInitConfig_t cfg;
            cfg.buff.size = 512;
            cfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            cfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
            cfg.maxElements = 4;
            cfg.name = "thread-pool";
            cfg.allocator = QC_MEMORY_ALLOCATOR_DMA;
            QCMemoryPoolHandle_t ph;
            ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( h, cfg, ph ) );
            QCBufferDescriptorBase_t buf{};
            ASSERT_EQ( QC_STATUS_OK, Ifs->AllocateBufferFromPool( ph, buf ) );
            ASSERT_NE( nullptr, buf.pBuf );
            ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( ph, buf ) );
            ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( ph ) );
            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_Pool_Operations_SameNode_8Threads %d itersPerThread %d\n", i,
                        itersPerThread );
            }
        }
    };
    std::vector<std::thread> ts;
    for ( int t = 0; t < threads; ++t ) ts.emplace_back( worker, t );
    start.store( true );
    for ( auto &th : ts ) th.join();
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
}
/*concurrent Buffer allocation from the same Node*/
TEST_P( Test_QCMemorymanager_Stress, Concurrent_AllocateBuffer_SameNode_8Threads )
{
    QCNodeID_t node = { "SharedNode", QC_NODE_TYPE_CUSTOM_0, 0 };
    QCMemoryHandle_t h;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );
    const int threads = 8;
    const int allocsPerThread = GetParam();
    std::cout << "Running with " << allocsPerThread << " allocations per thread" << std::endl;
    std::atomic<bool> start{ false };
    auto worker = [&]( int tid ) {
        while ( !start.load() ) std::this_thread::yield();
        for ( int i = 0; i < allocsPerThread; ++i )
        {
            QCBufferPropBase_t req{};
            req.size = 1024 + ( tid * 256 );   // Vary sizes
            req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            req.cache = QC_CACHEABLE;
            QCBufferDescriptorBase_t resp{};
            ASSERT_EQ( QC_STATUS_OK,
                       Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, resp ) );
            ASSERT_NE( nullptr, resp.pBuf );
            ASSERT_EQ( QC_STATUS_OK, Ifs->FreeBuffer( h, resp ) );
            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_AllocateBuffer_SameNode_8Threads %d itersPerThread %d\n", i,
                        allocsPerThread );
            }
        }
    };
    std::vector<std::thread> ts;
    for ( int t = 0; t < threads; ++t ) ts.emplace_back( worker, t );
    start.store( true );
    for ( auto &th : ts ) th.join();
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
}
/*concurrent reclaim and allocation*/
TEST_P( Test_QCMemorymanager_Stress, Concurrent_Allocate_vs_Reclaim_2Threads )
{
    QCNodeID_t node = { "ReclaimNode", QC_NODE_TYPE_CUSTOM_2, 2 };
    QCMemoryHandle_t h;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );
    const int reclaimAttempts = GetParam();
    std::cout << "Running with " << reclaimAttempts << " reclaim attempts" << std::endl;
    std::atomic<bool> start{ false };
    std::atomic<bool> stop{ false };
    auto allocator = [&]() {
        while ( !start.load() ) std::this_thread::yield();
        while ( !stop.load() )
        {
            QCBufferPropBase_t req{};
            req.size = 2048;
            req.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            req.cache = QC_CACHEABLE;
            QCBufferDescriptorBase_t resp{};
            QCStatus_e st = Ifs->AllocateBuffer( h, QC_MEMORY_ALLOCATOR_HEAP, req, resp );
            if ( st == QC_STATUS_OK )
            {
                // Optionally free immediately or let reclaim handle it
                std::this_thread::sleep_for( std::chrono::microseconds( 10 ) );
            }
        }
    };
    auto reclaimer = [&]() {
        while ( !start.load() ) std::this_thread::yield();
        for ( int i = 0; i < reclaimAttempts; ++i )
        {
            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_Allocate_vs_Reclaim_2Threads %d reclaimAttempts %d\n", i,
                        reclaimAttempts );
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            QCStatus_e st = Ifs->ReclaimResources( h );
            EXPECT_TRUE( st == QC_STATUS_OK ||
                         st == QC_STATUS_FAIL );   // May fail if allocator fails
        }
        stop.store( true );
    };
    std::thread t1( allocator );
    std::thread t2( reclaimer );
    start.store( true );
    t1.join();
    t2.join();
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
}
/*concurrent pool exhaustion and refill*/
TEST_P( Test_QCMemorymanager_Stress, Concurrent_Pool_Exhaustion_Refill_8Threads )
{
    QCNodeID_t node = { "ExhaustNode", QC_NODE_TYPE_CUSTOM_3, 3 };
    QCMemoryHandle_t h;
    ASSERT_EQ( QC_STATUS_OK, Ifs->Register( node, h ) );
    QCMemoryPoolInitConfig_t cfg;
    cfg.buff.size = 256;
    cfg.buff.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    cfg.buff.cache = QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES;
    cfg.maxElements = 16;   // Small pool
    cfg.name = "exhaust-pool";
    cfg.allocator = QC_MEMORY_ALLOCATOR_DMA;
    QCMemoryPoolHandle_t ph;
    ASSERT_EQ( QC_STATUS_OK, Ifs->CreatePool( h, cfg, ph ) );
    const int threads = 8;
    const int itersPerThread = GetParam();
    std::cout << "Running with " << itersPerThread << " iterations per thread" << std::endl;
    std::atomic<bool> start{ false };
    auto worker = [&]( int tid ) {
        while ( !start.load() ) std::this_thread::yield();
        for ( int i = 0; i < itersPerThread; ++i )
        {
            QCBufferDescriptorBase_t buf{};
            QCStatus_e st = Ifs->AllocateBufferFromPool( ph, buf );
            if ( st == QC_STATUS_OK )
            {
                ASSERT_NE( nullptr, buf.pBuf );
                // Hold briefly to increase contention
                std::this_thread::yield();
                ASSERT_EQ( QC_STATUS_OK, Ifs->PutBufferToPool( ph, buf ) );
            }
            else
            {
                // Pool exhausted, retry
                EXPECT_EQ( QC_STATUS_OUT_OF_BOUND, st );
            }

            if ( ( i % 10000 ) == 0 )
            {
                printf( "Concurrent_Pool_Exhaustion_Refill_8Threads %d itersPerThread %d\n", i,
                        itersPerThread );
            }
        }
    };
    std::vector<std::thread> ts;
    for ( int t = 0; t < threads; ++t ) ts.emplace_back( worker, t );
    start.store( true );
    for ( auto &th : ts ) th.join();
    ASSERT_EQ( QC_STATUS_OK, Ifs->DestroyPool( ph ) );
    ASSERT_EQ( QC_STATUS_OK, Ifs->UnRegister( h ) );
}
// Instantiate all stress and concurrent tests with different iteration counts
INSTANTIATE_TEST_SUITE_P( StressLevels, Test_QCMemorymanager_Stress,
                          ::testing::Values( 100,         // Quick smoke test
                                             1000,        // Light stress
                                             10000,       // Medium stress (original for most tests)
                                             50000,       // Heavy stress
                                             1000000000   // Extreme stress
                                             ),

                          // Custom test name generator for readable output
                          []( const ::testing::TestParamInfo<int> &info ) {
                              return "Iters_" + std::to_string( info.param );
                          } );
/*concurrent INitializtion/deinitializtaion*/
TEST_F( Test_QCMemorymanager, Concurrent_Init_DeInit_Negative_MultiThread_1000Iterations )
{
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST> allocs = {
            allocatorIfs1, allocatorIfs2, allocatorIfs1, allocatorIfs2,
            allocatorIfs1, allocatorIfs2, allocatorIfs1 };
    QCMemoryManagerInit_t mmInit( 4, allocs );
    // Test with 2, 4, 8 threads
    std::vector<int> threadCounts = { 2, 4, 8 };
    for ( int numThreads : threadCounts )
    {
        std::cout << "Testing with " << numThreads << " threads..." << std::endl;
        for ( int iter = 0; iter < 1000; ++iter )
        {
            ManagerLocal instance;
            std::atomic<int> initSuccess{ 0 };
            std::atomic<int> initFail{ 0 };
            std::atomic<bool> start{ false };
            auto worker = [&]() {
                while ( !start.load() )
                {
                    std::this_thread::yield();
                }
                QCStatus_e st = instance.Initialize( mmInit );
                if ( st == QC_STATUS_OK )
                {
                    initSuccess.fetch_add( 1 );
                }
                else
                {
                    initFail.fetch_add( 1 );
                }
            };
            std::vector<std::thread> threads;
            threads.reserve( numThreads );
            for ( int t = 0; t < numThreads; ++t )
            {
                threads.emplace_back( worker );
            }
            start.store( true );
            for ( auto &th : threads )
            {
                th.join();
            }
            // Validate: Only one should succeed
            EXPECT_EQ( 1, initSuccess.load() ) << "Threads=" << numThreads << ", Iteration=" << iter
                                               << ": Expected exactly 1 successful init";
            EXPECT_EQ( numThreads - 1, initFail.load() )
                    << "Threads=" << numThreads << ", Iteration=" << iter << ": Expected "
                    << ( numThreads - 1 ) << " failed inits";
            QCStatus_e deinitStatus = instance.DeInitialize();
            ASSERT_EQ( QC_STATUS_OK, deinitStatus )
                    << "Threads=" << numThreads << ", Iteration=" << iter
                    << ": DeInitialize failed";
        }
    }
}
