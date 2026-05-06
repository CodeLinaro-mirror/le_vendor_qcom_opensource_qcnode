// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/HeapAllocator.hpp"
#include "QC/Node/NodeFrameDescriptor.hpp"
#include "QC/Node/SimulationNode.hpp"
#include <chrono>
#include <gtest/gtest-printers.h>
#include <gtest/gtest.h>
#include <sstream>
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

// Define test case types
enum class TestCaseType
{
    BASIC_INIT,               // Basic initialization
    INVALID_CONFIG,           // Invalid configuration
    START_STOP,               // Start and stop
    DEINITIALIZE,             // Deinitialize
    SYNC_PROCESSING,          // Synchronous processing
    ASYNC_PROCESSING,         // Asynchronous processing
    ERROR_SIMULATION,         // Error simulation
    FORCED_STATE,             // Forced state
    RETURN_STATUS,            // Return status for functions
    PROCESS_INVALID_STATE,    // Process in invalid state
    DEINIT_WITHOUT_STOP,      // Deinitialize without stopping
    MULTIPLE_FRAMES,          // Multiple frame processing
    BUFFER_PASSTHROUGH,       // Buffer passthrough
    DIFFERENT_BUFFER_SIZES,   // Different buffer sizes
    NULL_BUFFERS,             // Null buffers
    CONFIG_OPTIONS            // Configuration options
};

// Holds test parameters for parameterized tests
struct SimulationNodeTestParams
{
    TestCaseType testType;             // Type of test to run
    bool isAsync;                      // Whether to use async processing mode
    uint32_t processingDelayMs;        // Processing delay in milliseconds
    std::string errorType;             // Type of error to simulate
    uint32_t errorRate;                // Error rate (0 means no errors)
    bool forceState;                   // Whether to force a specific state
    std::string forcedState;           // State to force if forceState is true
    std::string returnFunctionName;    // Function name for return status test
    int returnStatus;                  // Status to return for the function
    QCStatus_e expectedStatus;         // Expected status from operation
    QCObjectState_e expectedState;     // Expected state after operation
    std::vector<size_t> bufferSizes;   // Buffer sizes for different buffer size test
    int numFrames;                     // Number of frames to process
    std::string testName;              // Name of the test for identification

    // Constructor for easy parameter creation
    SimulationNodeTestParams( TestCaseType type, bool async = false, uint32_t delay = 0,
                              const std::string &error = "none", uint32_t rate = 0,
                              bool force = false, const std::string &state = "",
                              const std::string &funcName = "", int status = 0,
                              QCStatus_e expStatus = QC_STATUS_OK,
                              QCObjectState_e expState = QC_OBJECT_STATE_READY,
                              const std::vector<size_t> &bufSizes = { 1024, 1024, 1024, 1024 },
                              int frames = 1, const std::string &name = "" )
        : testType( type ),
          isAsync( async ),
          processingDelayMs( delay ),
          errorType( error ),
          errorRate( rate ),
          forceState( force ),
          forcedState( state ),
          returnFunctionName( funcName ),
          returnStatus( status ),
          expectedStatus( expStatus ),
          expectedState( expState ),
          bufferSizes( bufSizes ),
          numFrames( frames ),
          testName( name )
    {}

    // Generates a descriptive string for test case naming
    std::string ToString() const
    {
        if ( !testName.empty() )
        {
            return testName;
        }

        std::ostringstream oss;
        switch ( testType )
        {
            case TestCaseType::BASIC_INIT:
                oss << "BasicInit";
                break;
            case TestCaseType::INVALID_CONFIG:
                oss << "InvalidConfig";
                break;
            case TestCaseType::START_STOP:
                oss << "StartStop";
                break;
            case TestCaseType::DEINITIALIZE:
                oss << "Deinitialize";
                break;
            case TestCaseType::SYNC_PROCESSING:
                oss << "SyncProcessing";
                break;
            case TestCaseType::ASYNC_PROCESSING:
                oss << "AsyncProcessing";
                break;
            case TestCaseType::ERROR_SIMULATION:
                oss << "ErrorSim";
                break;
            case TestCaseType::FORCED_STATE:
                oss << "ForcedState";
                break;
            case TestCaseType::RETURN_STATUS:
                oss << "ReturnStatus";
                break;
            case TestCaseType::PROCESS_INVALID_STATE:
                oss << "ProcessInvalidState";
                break;
            case TestCaseType::DEINIT_WITHOUT_STOP:
                oss << "DeinitWithoutStop";
                break;
            case TestCaseType::MULTIPLE_FRAMES:
                oss << "MultipleFrames";
                break;
            case TestCaseType::BUFFER_PASSTHROUGH:
                oss << "BufferPassthrough";
                break;
            case TestCaseType::DIFFERENT_BUFFER_SIZES:
                oss << "DifferentBufferSizes";
                break;
            case TestCaseType::NULL_BUFFERS:
                oss << "NullBuffers";
                break;
            case TestCaseType::CONFIG_OPTIONS:
                oss << "ConfigOptions";
                break;
        }

        if ( isAsync )
        {
            oss << "_Async";
        }

        if ( processingDelayMs > 0 )
        {
            oss << "_Delay" << processingDelayMs;
        }

        if ( errorType != "none" )
        {
            oss << "_Error" << errorType;
        }

        if ( errorRate > 0 )
        {
            oss << "_Rate" << errorRate;
        }

        if ( forceState )
        {
            oss << "_Force" << forcedState;
        }

        if ( !returnFunctionName.empty() )
        {
            oss << "_Return" << returnFunctionName << returnStatus;
        }

        if ( numFrames > 1 )
        {
            oss << "_Frames" << numFrames;
        }

        return oss.str();
    }
};

// Add custom printer for SimulationNodeTestParams
namespace testing
{
void PrintTo( const SimulationNodeTestParams &params, std::ostream *os )
{
    *os << params.ToString();
}

// Specialization for internal printing mechanism
namespace internal2
{
template<>
struct TypeWithoutFormatter<SimulationNodeTestParams, (testing::internal2::TypeKind) 2>
{
    static void PrintValue( const SimulationNodeTestParams &value, ::std::ostream *os )
    {
        *os << value.ToString();
    }
};
}   // namespace internal2
}   // namespace testing

// Parameterized test fixture for SimulationNode tests
class SimulationNodeParamTest : public ::testing::TestWithParam<SimulationNodeTestParams>
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

    // Creates a configuration based on test parameters
    std::string CreateConfigFromParams( const SimulationNodeTestParams &params )
    {
        std::string config = R"({
            "static": {
                "name": "TestSimulationNode",
                "id": 1,
                "processingMode": ")" +
                             std::string( params.isAsync ? "async" : "sync" ) + R"(",
                "processingDelayMs": )" +
                             std::to_string( params.processingDelayMs ) + R"(,
                "numInputs": 2,
                "numOutputs": 2,
                "errorType": ")" +
                             params.errorType + R"(",
                "errorRate": )" +
                             std::to_string( params.errorRate ) + R"(,
                "forceState": )" +
                             ( params.forceState ? "true" : "false" ) + R"(,)";

        if ( params.forceState )
        {
            config += R"(
                "forcedState": ")" +
                      params.forcedState + R"(",)";
        }

        if ( !params.returnFunctionName.empty() )
        {
            config += R"(
                "returnStatusForFunctions": [
                    {"function": ")" +
                      params.returnFunctionName + R"(", "status": )" +
                      std::to_string( params.returnStatus ) + R"(}
                ],)";
        }

        config += R"(
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

    // Creates an invalid configuration for testing
    std::string CreateInvalidConfig()
    {
        return R"({
            "static": {
                "name": "",
                "id": 1
            }
        })";
    }

    SimulationNode *m_pSimulationNode = nullptr;
    bool m_callbackCalled = false;
    QCStatus_e m_lastCallbackStatus = QC_STATUS_OK;
};

// Generates a name for each test case based on its parameters
std::string ParamToTestName( const ::testing::TestParamInfo<SimulationNodeTestParams> &info )
{
    return info.param.ToString();
}

// Parameterized test that runs different test scenarios based on parameters
TEST_P( SimulationNodeParamTest, SimulationNodeTests )
{
    SimulationNodeTestParams params = GetParam();

    // Create buffer descriptors based on the test type
    std::vector<BufferDescriptor_t> bufferDescs;

    if ( params.testType == TestCaseType::NULL_BUFFERS )
    {
        // Input 0: Normal buffer
        BufferDescriptor_t input0 = CreateBufferDescriptor( 0, 512 );

        // Input 1: Null buffer
        BufferDescriptor_t input1;
        input1.id = 1;
        input1.size = 0;
        input1.pBuf = nullptr;

        // Output 0: Normal buffer
        BufferDescriptor_t output0 = CreateBufferDescriptor( 2, 512 );

        // Output 1: Null buffer
        BufferDescriptor_t output1;
        output1.id = 3;
        output1.size = 0;
        output1.pBuf = nullptr;

        bufferDescs.push_back( input0 );
        bufferDescs.push_back( input1 );
        bufferDescs.push_back( output0 );
        bufferDescs.push_back( output1 );
    }
    else if ( params.testType == TestCaseType::DIFFERENT_BUFFER_SIZES )
    {
        for ( uint32_t i = 0; i < 4 && i < params.bufferSizes.size(); i++ )
        {
            bufferDescs.push_back( CreateBufferDescriptor( i, params.bufferSizes[i] ) );
        }
    }
    else
    {
        for ( uint32_t i = 0; i < 4; i++ )
        {
            bufferDescs.push_back( CreateBufferDescriptor( i, 1024 ) );
        }
    }

    // Create configuration based on test type
    std::string config = ( params.testType == TestCaseType::INVALID_CONFIG )
                                 ? CreateInvalidConfig()
                                 : CreateConfigFromParams( params );

    // Initialize the node
    QCNodeInit_t nodeInit;
    nodeInit.config = config;
    nodeInit.buffers.reserve( bufferDescs.size() );
    for ( size_t i = 0; i < bufferDescs.size(); i++ )
    {
        nodeInit.buffers.push_back( bufferDescs[i] );
    }

    // Set callback for async mode
    nodeInit.callback = [this]( const QCNodeEventInfo_t &eventInfo ) {
        m_callbackCalled = true;
        m_lastCallbackStatus = eventInfo.status;
    };

    // Initialize the node
    QCStatus_e status = m_pSimulationNode->Initialize( nodeInit );

    // Check initialization results based on test type
    switch ( params.testType )
    {
        case TestCaseType::INVALID_CONFIG:
            EXPECT_NE( status, QC_STATUS_OK );
            break;

        case TestCaseType::FORCED_STATE:
            if ( params.forcedState == "running" )
            {
                EXPECT_TRUE( status == QC_STATUS_OK || status == QC_STATUS_BAD_STATE );
            }
            else
            {
                EXPECT_EQ( status, QC_STATUS_OK );
            }
            EXPECT_EQ( m_pSimulationNode->GetState(), params.expectedState );
            break;

        default:
            EXPECT_EQ( status, QC_STATUS_OK );
            break;
    }

    // If initialization failed and we're not testing invalid config, no need to continue
    if ( status != QC_STATUS_OK && params.testType != TestCaseType::INVALID_CONFIG )
    {
        for ( auto &bufDesc : bufferDescs )
        {
            if ( bufDesc.pBuf )
            {
                FreeBufferDescriptor( bufDesc );
            }
        }
        return;
    }

    // Execute test based on test type
    switch ( params.testType )
    {
        case TestCaseType::BASIC_INIT:
            EXPECT_EQ( m_pSimulationNode->GetState(), params.expectedState );
            break;

        case TestCaseType::START_STOP:
            status = m_pSimulationNode->Start();
            EXPECT_EQ( status, params.expectedStatus );
            if ( status == QC_STATUS_OK )
            {
                EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_RUNNING );
            }

            status = m_pSimulationNode->Stop();
            EXPECT_EQ( status, params.expectedStatus );
            if ( status == QC_STATUS_OK )
            {
                EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_READY );
            }
            break;

        case TestCaseType::DEINITIALIZE:
            status = m_pSimulationNode->DeInitialize();
            EXPECT_EQ( status, params.expectedStatus );
            if ( status == QC_STATUS_OK )
            {
                EXPECT_EQ( m_pSimulationNode->GetState(), QC_OBJECT_STATE_INITIAL );
            }
            break;

        case TestCaseType::SYNC_PROCESSING:
        case TestCaseType::ASYNC_PROCESSING:
        case TestCaseType::BUFFER_PASSTHROUGH:
            if ( m_pSimulationNode->GetState() != QC_OBJECT_STATE_RUNNING )
            {
                status = m_pSimulationNode->Start();
                EXPECT_EQ( status, QC_STATUS_OK );
            }

            {
                std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );

                for ( uint32_t i = 0; i < bufferDescs.size(); i++ )
                {
                    status = frameDesc->SetBuffer( i, bufferDescs[i] );
                    EXPECT_EQ( status, QC_STATUS_OK );
                }

                // Fill input buffers with test data
                if ( params.testType == TestCaseType::BUFFER_PASSTHROUGH )
                {
                    if ( bufferDescs[0].pBuf )
                    {
                        uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                        for ( size_t i = 0; i < bufferDescs[0].size; i++ )
                        {
                            pInput0[i] = static_cast<uint8_t>( i & 0xFF );
                        }
                    }

                    if ( bufferDescs[1].pBuf )
                    {
                        uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
                        for ( size_t i = 0; i < bufferDescs[1].size; i++ )
                        {
                            pInput1[i] = static_cast<uint8_t>( ( 255 - i ) & 0xFF );
                        }
                    }
                }
                else
                {
                    if ( bufferDescs[0].pBuf )
                    {
                        uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                        for ( size_t i = 0; i < bufferDescs[0].size; i++ )
                        {
                            pInput0[i] = static_cast<uint8_t>( i & 0xFF );
                        }
                    }

                    if ( bufferDescs[1].pBuf )
                    {
                        uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
                        for ( size_t i = 0; i < bufferDescs[1].size; i++ )
                        {
                            pInput1[i] = static_cast<uint8_t>( ( i + 128 ) & 0xFF );
                        }
                    }
                }

                // Clear output buffers
                if ( bufferDescs[2].pBuf )
                {
                    memset( bufferDescs[2].pBuf, 0, bufferDescs[2].size );
                }
                if ( bufferDescs[3].pBuf )
                {
                    memset( bufferDescs[3].pBuf, 0, bufferDescs[3].size );
                }

                m_callbackCalled = false;

                status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
                EXPECT_EQ( status, params.expectedStatus );

                if ( params.isAsync && status == QC_STATUS_OK )
                {
                    EXPECT_TRUE( m_callbackCalled );
                    EXPECT_EQ( m_lastCallbackStatus, QC_STATUS_OK );
                }

                if ( params.processingDelayMs > 0 )
                {
                    std::this_thread::sleep_for(
                            std::chrono::milliseconds( params.processingDelayMs + 50 ) );
                }

                // Verify output buffers
                if ( status == QC_STATUS_OK )
                {
                    if ( params.testType == TestCaseType::DIFFERENT_BUFFER_SIZES )
                    {
                        if ( bufferDescs[0].pBuf && bufferDescs[2].pBuf )
                        {
                            size_t copySize = std::min( bufferDescs[0].size, bufferDescs[2].size );
                            uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                            uint8_t *pOutput0 = static_cast<uint8_t *>( bufferDescs[2].pBuf );
                            for ( size_t i = 0; i < copySize; i++ )
                            {
                                EXPECT_EQ( pOutput0[i], pInput0[i] )
                                        << "Mismatch at index " << i << " for Output 0";
                            }
                        }

                        if ( bufferDescs[1].pBuf && bufferDescs[3].pBuf )
                        {
                            size_t copySize = std::min( bufferDescs[1].size, bufferDescs[3].size );
                            uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
                            uint8_t *pOutput1 = static_cast<uint8_t *>( bufferDescs[3].pBuf );
                            for ( size_t i = 0; i < copySize; i++ )
                            {
                                EXPECT_EQ( pOutput1[i], pInput1[i] )
                                        << "Mismatch at index " << i << " for Output 1";
                            }
                        }
                    }
                    else if ( params.testType == TestCaseType::NULL_BUFFERS )
                    {
                        if ( bufferDescs[0].pBuf && bufferDescs[2].pBuf )
                        {
                            uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                            uint8_t *pOutput0 = static_cast<uint8_t *>( bufferDescs[2].pBuf );
                            for ( size_t i = 0; i < bufferDescs[0].size; i++ )
                            {
                                EXPECT_EQ( pOutput0[i], pInput0[i] )
                                        << "Mismatch at index " << i << " for Output 0";
                            }
                        }
                    }
                    else
                    {
                        if ( bufferDescs[0].pBuf && bufferDescs[2].pBuf )
                        {
                            uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                            uint8_t *pOutput0 = static_cast<uint8_t *>( bufferDescs[2].pBuf );
                            for ( size_t i = 0; i < bufferDescs[0].size; i++ )
                            {
                                EXPECT_EQ( pOutput0[i], pInput0[i] )
                                        << "Mismatch at index " << i << " for Output 0";
                            }
                        }

                        if ( bufferDescs[1].pBuf && bufferDescs[3].pBuf )
                        {
                            uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
                            uint8_t *pOutput1 = static_cast<uint8_t *>( bufferDescs[3].pBuf );
                            for ( size_t i = 0; i < bufferDescs[1].size; i++ )
                            {
                                EXPECT_EQ( pOutput1[i], pInput1[i] )
                                        << "Mismatch at index " << i << " for Output 1";
                            }
                        }
                    }
                }
            }

            status = m_pSimulationNode->Stop();
            EXPECT_EQ( status, QC_STATUS_OK );
            break;

        case TestCaseType::ERROR_SIMULATION:
            status = m_pSimulationNode->Start();
            EXPECT_EQ( status, QC_STATUS_OK );

            {
                std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );

                for ( uint32_t i = 0; i < bufferDescs.size(); i++ )
                {
                    status = frameDesc->SetBuffer( i, bufferDescs[i] );
                    EXPECT_EQ( status, QC_STATUS_OK );
                }

                status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
                EXPECT_EQ( status, params.expectedStatus );
            }

            status = m_pSimulationNode->Stop();
            EXPECT_EQ( status, QC_STATUS_OK );
            break;

        case TestCaseType::RETURN_STATUS:
            if ( params.returnFunctionName == "Start" )
            {
                status = m_pSimulationNode->Start();
                EXPECT_EQ( status, static_cast<QCStatus_e>( params.returnStatus ) );
            }
            else if ( params.returnFunctionName == "Stop" )
            {
                m_pSimulationNode->Start();
                status = m_pSimulationNode->Stop();
                EXPECT_EQ( status, static_cast<QCStatus_e>( params.returnStatus ) );
            }
            else if ( params.returnFunctionName == "DeInitialize" )
            {
                status = m_pSimulationNode->DeInitialize();
                EXPECT_EQ( status, static_cast<QCStatus_e>( params.returnStatus ) );
            }
            else if ( params.returnFunctionName == "ProcessFrameDescriptor" )
            {
                m_pSimulationNode->Start();
                std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );
                for ( uint32_t i = 0; i < bufferDescs.size(); i++ )
                {
                    frameDesc->SetBuffer( i, bufferDescs[i] );
                }
                status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
                EXPECT_EQ( status, static_cast<QCStatus_e>( params.returnStatus ) );
                m_pSimulationNode->Stop();
            }
            break;

        case TestCaseType::PROCESS_INVALID_STATE:
        {
            std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );
            for ( uint32_t i = 0; i < bufferDescs.size(); i++ )
            {
                status = frameDesc->SetBuffer( i, bufferDescs[i] );
                EXPECT_EQ( status, QC_STATUS_OK );
            }

            status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
            EXPECT_EQ( status, QC_STATUS_BAD_STATE );
        }
        break;

        case TestCaseType::DEINIT_WITHOUT_STOP:
            status = m_pSimulationNode->Start();
            EXPECT_EQ( status, QC_STATUS_OK );

            status = m_pSimulationNode->DeInitialize();
            EXPECT_EQ( status, QC_STATUS_BAD_STATE );

            status = m_pSimulationNode->Stop();
            EXPECT_EQ( status, QC_STATUS_OK );

            status = m_pSimulationNode->DeInitialize();
            EXPECT_EQ( status, QC_STATUS_OK );
            break;

        case TestCaseType::MULTIPLE_FRAMES:
            status = m_pSimulationNode->Start();
            EXPECT_EQ( status, QC_STATUS_OK );

            {
                std::unique_ptr<NodeFrameDescriptor> frameDesc( CreateFrameDescriptor( 4 ) );

                for ( uint32_t i = 0; i < bufferDescs.size(); i++ )
                {
                    status = frameDesc->SetBuffer( i, bufferDescs[i] );
                    EXPECT_EQ( status, QC_STATUS_OK );
                }

                for ( int i = 0; i < params.numFrames; i++ )
                {
                    uint8_t *pInput0 = static_cast<uint8_t *>( bufferDescs[0].pBuf );
                    uint8_t *pInput1 = static_cast<uint8_t *>( bufferDescs[1].pBuf );
                    for ( size_t j = 0; j < bufferDescs[0].size; j++ )
                    {
                        pInput0[j] = static_cast<uint8_t>( ( i * 10 + j ) & 0xFF );
                    }
                    for ( size_t j = 0; j < bufferDescs[1].size; j++ )
                    {
                        pInput1[j] = static_cast<uint8_t>( ( i * 10 + j + 128 ) & 0xFF );
                    }

                    status = m_pSimulationNode->ProcessFrameDescriptor( *frameDesc );
                    EXPECT_EQ( status, QC_STATUS_OK );

                    uint8_t *pOutput0 = static_cast<uint8_t *>( bufferDescs[2].pBuf );
                    uint8_t *pOutput1 = static_cast<uint8_t *>( bufferDescs[3].pBuf );
                    for ( size_t j = 0; j < bufferDescs[0].size; j++ )
                    {
                        EXPECT_EQ( pOutput0[j], pInput0[j] );
                    }
                    for ( size_t j = 0; j < bufferDescs[1].size; j++ )
                    {
                        EXPECT_EQ( pOutput1[j], pInput1[j] );
                    }
                }
            }

            status = m_pSimulationNode->Stop();
            EXPECT_EQ( status, QC_STATUS_OK );
            break;

        case TestCaseType::CONFIG_OPTIONS:
        {
            const std::string &options = m_pSimulationNode->GetConfigurationIfs().GetOptions();
            EXPECT_FALSE( options.empty() );
            EXPECT_NE( options.find( "version" ), std::string::npos );
            EXPECT_NE( options.find( "processingMode" ), std::string::npos );
        }
        break;

        default:
            break;
    }

    // Clean up buffer descriptors
    for ( auto &bufDesc : bufferDescs )
    {
        if ( bufDesc.pBuf )
        {
            FreeBufferDescriptor( bufDesc );
        }
    }
}

// Instantiate the test with different parameters
INSTANTIATE_TEST_SUITE_P(
        SimulationNodeTests, SimulationNodeParamTest,
        ::testing::Values(
                // Basic initialization
                SimulationNodeTestParams( TestCaseType::BASIC_INIT, false, 0, "none", 0, false, "",
                                          "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "BasicInitialization" ),

                // Invalid configuration
                SimulationNodeTestParams( TestCaseType::INVALID_CONFIG, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_BAD_ARGUMENTS,
                                          QC_OBJECT_STATE_INITIAL, { 1024, 1024, 1024, 1024 }, 1,
                                          "InvalidConfiguration" ),

                // Start and stop
                SimulationNodeTestParams( TestCaseType::START_STOP, false, 0, "none", 0, false, "",
                                          "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "StartStop" ),

                // Deinitialize
                SimulationNodeTestParams( TestCaseType::DEINITIALIZE, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_INITIAL,
                                          { 1024, 1024, 1024, 1024 }, 1, "Deinitialize" ),

                // Synchronous processing
                SimulationNodeTestParams( TestCaseType::SYNC_PROCESSING, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "SyncProcessing" ),

                // Asynchronous processing
                SimulationNodeTestParams( TestCaseType::ASYNC_PROCESSING, true, 100, "none", 0,
                                          false, "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "AsyncProcessing" ),

                // Error simulation
                SimulationNodeTestParams( TestCaseType::ERROR_SIMULATION, false, 0, "bad_arguments",
                                          1, false, "", "", 0, QC_STATUS_BAD_ARGUMENTS,
                                          QC_OBJECT_STATE_READY, { 1024, 1024, 1024, 1024 }, 1,
                                          "ErrorSimulation" ),

                // Forced state - running
                SimulationNodeTestParams( TestCaseType::FORCED_STATE, false, 0, "none", 0, true,
                                          "running", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_RUNNING,
                                          { 1024, 1024, 1024, 1024 }, 1, "ForcedStateRunning" ),

                // Forced state - ready
                SimulationNodeTestParams( TestCaseType::FORCED_STATE, false, 0, "none", 0, true,
                                          "ready", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "ForcedStateReady" ),

                // Forced state - error
                SimulationNodeTestParams( TestCaseType::FORCED_STATE, false, 0, "none", 0, true,
                                          "error", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_ERROR,
                                          { 1024, 1024, 1024, 1024 }, 1, "ForcedStateError" ),

                // Return status for Start function
                SimulationNodeTestParams( TestCaseType::RETURN_STATUS, false, 0, "none", 0, false,
                                          "", "Start", static_cast<int>( QC_STATUS_FAIL ),
                                          QC_STATUS_FAIL, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "ReturnStatusStart" ),

                // Process in invalid state
                SimulationNodeTestParams( TestCaseType::PROCESS_INVALID_STATE, false, 0, "none", 0,
                                          false, "", "", 0, QC_STATUS_BAD_STATE,
                                          QC_OBJECT_STATE_READY, { 1024, 1024, 1024, 1024 }, 1,
                                          "ProcessInvalidState" ),

                // Deinitialize without stopping
                SimulationNodeTestParams( TestCaseType::DEINIT_WITHOUT_STOP, false, 0, "none", 0,
                                          false, "", "", 0, QC_STATUS_BAD_STATE,
                                          QC_OBJECT_STATE_READY, { 1024, 1024, 1024, 1024 }, 1,
                                          "DeinitWithoutStop" ),

                // Multiple frame processing
                SimulationNodeTestParams( TestCaseType::MULTIPLE_FRAMES, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 5,
                                          "MultipleFrameProcessing" ),

                // Buffer passthrough
                SimulationNodeTestParams( TestCaseType::BUFFER_PASSTHROUGH, false, 0, "none", 0,
                                          false, "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "BufferPassthrough" ),

                // Different buffer sizes
                SimulationNodeTestParams( TestCaseType::DIFFERENT_BUFFER_SIZES, false, 0, "none", 0,
                                          false, "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 512, 1024, 256, 2048 }, 1, "DifferentBufferSizes" ),

                // Null buffers
                SimulationNodeTestParams( TestCaseType::NULL_BUFFERS, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 512, 0, 512, 0 }, 1, "NullBuffers" ),

                // Configuration options
                SimulationNodeTestParams( TestCaseType::CONFIG_OPTIONS, false, 0, "none", 0, false,
                                          "", "", 0, QC_STATUS_OK, QC_OBJECT_STATE_READY,
                                          { 1024, 1024, 1024, 1024 }, 1, "ConfigurationOptions" ) ),
        ParamToTestName );

int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    std::cout << "Running SimulationNode parameterized tests..." << std::endl;
    return RUN_ALL_TESTS();
}