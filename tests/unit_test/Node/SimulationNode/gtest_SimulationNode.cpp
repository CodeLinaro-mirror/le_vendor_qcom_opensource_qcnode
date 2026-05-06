// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/HeapAllocator.hpp"
#include "QC/Node/NodeFrameDescriptor.hpp"
#include "QC/Node/SimulationNode.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using namespace QC;
using namespace QC::Node;

// Define the static member of NodeFrameDescriptor
QC::Node::QCDummyBufferDescriptor_t QC::Node::NodeFrameDescriptor::s_dummy;

// Creates a buffer descriptor with specified ID and size using HeapAllocator
BufferDescriptor_t CreateBufferDescriptor( uint32_t id, size_t size )
{
    static QC::Memory::HeapAllocator allocator;

    BufferDescriptor_t bufDesc;
    bufDesc.id = id;
    bufDesc.size = size;

    QC::Memory::QCBufferPropBase_t request;
    request.size = size;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;

    QC::Memory::QCBufferDescriptorBase_t response;
    QCStatus_e status = allocator.Allocate( request, response );

    if ( status == QC_STATUS_OK )
    {
        bufDesc.pBuf = response.pBuf;
        bufDesc.allocatorType = response.allocatorType;
        memset( bufDesc.pBuf, 0xAA, size );
    }
    else
    {
        bufDesc.pBuf = nullptr;
    }

    return bufDesc;
}

// Frees a buffer descriptor using HeapAllocator
void FreeBufferDescriptor( BufferDescriptor_t &bufDesc )
{
    static QC::Memory::HeapAllocator allocator;

    if ( bufDesc.pBuf )
    {
        QC::Memory::QCBufferDescriptorBase_t freeDesc;
        freeDesc.pBuf = bufDesc.pBuf;
        freeDesc.size = bufDesc.size;
        freeDesc.allocatorType = bufDesc.allocatorType;

        allocator.Free( freeDesc );
        bufDesc.pBuf = nullptr;
    }
}

// Creates a frame descriptor with specified number of buffers
NodeFrameDescriptor *CreateFrameDescriptor( uint32_t numBuffers )
{
    return new NodeFrameDescriptor( numBuffers );
}

// Test fixture for SimulationNode tests
class SimulationNodeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_pSimulationNode = static_cast<SimulationNode *>( CreateSimulationNode() );
        ASSERT_NE( m_pSimulationNode, nullptr );
    }

    void TearDown() override
    {
        if ( m_pSimulationNode )
        {
            if ( m_pSimulationNode->GetState() == QC_OBJECT_STATE_RUNNING )
            {
                m_pSimulationNode->Stop();
            }
            if ( m_pSimulationNode->GetState() == QC_OBJECT_STATE_READY )
            {
                m_pSimulationNode->DeInitialize();
            }
            delete m_pSimulationNode;
            m_pSimulationNode = nullptr;
        }
    }

    // Creates a basic configuration with optional async mode and delay
    std::string CreateBasicConfig( bool isAsync = false, uint32_t delayMs = 0 )
    {
        std::string processingMode = isAsync ? "async" : "sync";
        std::string config = R"({
            "static": {
                "name": "TestSimulationNode",
                "id": 1,
                "processingMode": ")" +
                             processingMode + R"(",
                "processingDelayMs": )" +
                             std::to_string( delayMs ) + R"(,
                "numInputs": 2,
                "numOutputs": 2,
                "errorType": "none",
                "errorRate": 0,
                "forceState": false,
                "bufferIds": [0, 1, 2, 3],
                "globalBufferIdMap": [
                    {"name": "input0", "id": 0},
                    {"name": "input1", "id": 1},
                    {"name": "output0", "id": 2},
                    {"name": "output1", "id": 3}
                ],
                "deRegisterAllBuffersWhenStop": false
            }
        })";
        return config;
    }

    // Creates a configuration with error simulation
    std::string CreateErrorConfig( const std::string &errorType, uint32_t errorRate )
    {
        std::string config = R"({
            "static": {
                "name": "TestSimulationNode",
                "id": 1,
                "processingMode": "sync",
                "processingDelayMs": 0,
                "numInputs": 2,
                "numOutputs": 2,
                "errorType": ")" +
                             errorType + R"(",
                "errorRate": )" +
                             std::to_string( errorRate ) + R"(,
                "forceState": false,
                "bufferIds": [0, 1, 2, 3],
                "globalBufferIdMap": [
                    {"name": "input0", "id": 0},
                    {"name": "input1", "id": 1},
                    {"name": "output0", "id": 2},
                    {"name": "output1", "id": 3}
                ],
                "deRegisterAllBuffersWhenStop": false
            }
        })";
        return config;
    }

    // Creates a configuration with forced state
    std::string CreateForcedStateConfig( const std::string &state )
    {
        std::string config = R"({
            "static": {
                "name": "TestSimulationNode",
                "id": 1,
                "processingMode": "sync",
                "processingDelayMs": 0,
                "numInputs": 2,
                "numOutputs": 2,
                "errorType": "none",
                "errorRate": 0,
                "forceState": true,
                "forcedState": ")" +
                             state + R"(",
                "bufferIds": [0, 1, 2, 3],
                "globalBufferIdMap": [
                    {"name": "input0", "id": 0},
                    {"name": "input1", "id": 1},
                    {"name": "output0", "id": 2},
                    {"name": "output1", "id": 3}
                ],
                "deRegisterAllBuffersWhenStop": false
            }
        })";
        return config;
    }

    // Creates a configuration with specific return status for functions
    std::string CreateReturnStatusConfig( const std::string &function, int status )
    {
        std::string config = R"({
            "static": {
                "name": "TestSimulationNode",
                "id": 1,
                "processingMode": "sync",
                "processingDelayMs": 0,
                "numInputs": 2,
                "numOutputs": 2,
                "errorType": "none",
                "errorRate": 0,
                "forceState": false,
                "bufferIds": [0, 1, 2, 3],
                "globalBufferIdMap": [
                    {"name": "input0", "id": 0},
                    {"name": "input1", "id": 1},
                    {"name": "output0", "id": 2},
                    {"name": "output1", "id": 3}
                ],
                "deRegisterAllBuffersWhenStop": false,
                "returnStatusForFunctions": [
                    {"function": ")" +
                             function + R"(", "status": )" + std::to_string( status ) + R"(}
                ]
            }
        })";
        return config;
    }

    SimulationNode *m_pSimulationNode = nullptr;
    bool m_callbackCalled = false;
    QCStatus_e m_lastCallbackStatus = QC_STATUS_OK;
};

// Tests basic initialization of SimulationNode
TEST_F( SimulationNodeTest, Initialize )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = CreateBasicConfig();

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_EQ( status, QC_STATUS_OK );
    EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_READY );

    for ( auto &bufDesc : bufferDescs )
    {
        FreeBufferDescriptor( bufDesc );
    }
}

// Tests initialization with invalid configuration
TEST_F( SimulationNodeTest, InitializeInvalidConfig )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = R"({
        "static": {
            "name": "",
            "id": 1
        }
    })";

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_NE( status, QC_STATUS_OK );

    for ( auto &bufDesc : bufferDescs )
    {
        FreeBufferDescriptor( bufDesc );
    }
}

// Tests start and stop functionality
TEST_F( SimulationNodeTest, StartStop )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = CreateBasicConfig();

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_EQ( status, QC_STATUS_OK );

    status = m_pSimulationNode->Start();
    EXPECT_EQ( status, QC_STATUS_OK );
    EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_RUNNING );

    status = m_pSimulationNode->Stop();
    EXPECT_EQ( status, QC_STATUS_OK );
    EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_READY );

    for ( auto &bufDesc : bufferDescs )
    {
        FreeBufferDescriptor( bufDesc );
    }
}

// Tests deinitialization
TEST_F( SimulationNodeTest, Deinitialize )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = CreateBasicConfig();

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_EQ( status, QC_STATUS_OK );

    status = m_pSimulationNode->DeInitialize();
    EXPECT_EQ( status, QC_STATUS_OK );
    EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_INITIAL );

    for ( auto &bufDesc : bufferDescs )
    {
        FreeBufferDescriptor( bufDesc );
    }
}

// Tests synchronous processing
TEST_F( SimulationNodeTest, SyncProcessing )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = CreateBasicConfig( false );

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_EQ( status, QC_STATUS_OK );

    status = m_pSimulationNode->Start();
    EXPECT_EQ( status, QC_STATUS_OK );

    std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );

    // Set input buffers
    for ( uint32_t i = 0; i < 2; i++ )
    {
        status = frameDesc->SetBuffer( i, bufferDescs[i] );
        EXPECT_EQ( status, QC_STATUS_OK );
    }

    // Set output buffers
    for ( uint32_t i = 2; i < 4; i++ )
    {
        status = frameDesc->SetBuffer( i, bufferDescs[i] );
        EXPECT_EQ( status, QC_STATUS_OK );
    }

    // Fill input buffers with test data
    uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
    uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
    for ( size_t i = 0; i < 1024; i++ )
    {
        pInput0[i] = static_cast<uint8_t>( i & 0xFF );
        pInput1[i] = static_cast<uint8_t>( ( i + 128 ) & 0xFF );
    }

    status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
    EXPECT_EQ( status, QC_STATUS_OK );

    // Verify output buffers
    uint8_t *pOutput0 = static_cast<uint8_t *>( bufferDescs[2].pBuf );
    uint8_t *pOutput1 = static_cast<uint8_t *>( bufferDescs[3].pBuf );
    for ( size_t i = 0; i < 1024; i++ )
    {
        EXPECT_EQ( pOutput0[i], pInput0[i] );
        EXPECT_EQ( pOutput1[i], pInput1[i] );
    }

    status = m_pSimulationNode->Stop();
    EXPECT_EQ( status, QC_STATUS_OK );

    for ( auto &bufDesc : bufferDescs )
    {
        FreeBufferDescriptor( bufDesc );
    }
}

// Tests asynchronous processing
TEST_F( SimulationNodeTest, AsyncProcessing )
{
    std::vector<BufferDescriptor_t> bufferDescs;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
    }

    std::string config = CreateBasicConfig( true, 100 );

    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    m_callbackCalled = false;
    nodeInit.callback = [this]( const QCNodeEventInfo_t &eventInfo ) {
        m_callbackCalled = true;
        m_lastCallbackStatus = eventInfo.status;
    };

    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );
    EXPECT_EQ( status, QC_STATUS_OK );

    status = m_pSimulationNode->Start();
    EXPECT_EQ( status, QC_STATUS_OK );

    std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );

    // Set input and output buffers
    for ( uint32_t i = 0; i < 4; i++ )
    {
        status = frameDesc->SetBuffer( i, bufferDescs[i] );
        EXPECT_EQ( status, QC_STATUS_OK );
    }

    // Fill input buffers with test data
    uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
    uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
    for ( size_t i = 0; i < 1024; i++ )
    {
        pInput0[i] = static_cast<uint8_t>( i & 0xFF );
        pInput1[i] = static_cast<uint8_t>( ( i + 128 ) & 0xFF );
    }

    status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
    EXPECT_EQ( status, QC_STATUS_OK );

    // Wait for callback
    int timeoutMs = 1000;
    while ( !m_callbackCalled && timeoutMs > 0 )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        timeoutMs -= 10;
    }

    EXPECT_TRUE( m_callbackC