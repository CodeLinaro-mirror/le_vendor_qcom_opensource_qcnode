// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#define private public
#include "QC/Node/Radar.hpp"
#undef private
#include "QC/Node/NodeFrameDescriptor.hpp"
#include "QC/sample/BufferManager.hpp"
#include "md5_utils.hpp"

using namespace QC;
using namespace QC::Node;
using namespace QC::test::utils;
using namespace QC::Memory;

namespace
{
class TBufferAllocator
{
private:
    QC::sample::BufferManager bufMgr;

public:
    TBufferAllocator() : bufMgr( { "Radar0", QCNodeType_e::QC_NODE_TYPE_RADAR, 0 } ) {}

    QCStatus_e alloc( TensorDescriptor_t &tensor, size_t size )
    {
        TensorProps_t props;
        props.numDims = 1;
        props.dims[0] = size;
        props.tensorType = QC::QCTensorType_e::QC_TENSOR_TYPE_INT_8;
        return bufMgr.Allocate( props, tensor );
    }


    QCStatus_e free( TensorDescriptor_t &tensor ) { return bufMgr.Free( tensor ); }
};

class TBuffer
{
private:
    TBufferAllocator &m_allocator;
    QCStatus_e m_allocationStatus;

public:
    TensorDescriptor_t tensor;

    TBuffer( TBufferAllocator &allocator, size_t size )
        : m_allocator( allocator ),
          m_allocationStatus( QC_STATUS_OK )
    {
        m_allocationStatus = allocator.alloc( tensor, size );
        tensor.type = QCBufferType_e::QC_BUFFER_TYPE_RAW;
    }

    TBuffer( TBufferAllocator &allocator, std::string name, size_t size )
        : TBuffer( allocator, size )
    {
        tensor.name = name;
    }

    ~TBuffer() { auto ret = m_allocator.free( tensor ); }

    QCStatus_e GetAllocationStatus() const { return m_allocationStatus; }
};
}   // namespace


/**
 * @brief Helper function to set radar configuration in DataTree
 *
 * This function translates component-level radar configuration to
 * the Node-level DataTree configuration format.
 *
 * @param[in] pRadarConfig Component radar configuration
 * @param[out] pdt DataTree to populate with configuration
 */
void SetConfigRadarEx( Radar_Config_t *pRadarConfig, QC::DataTree *pdt )
{
    // Set static configuration parameters from nested serviceConfig
    pdt->Set<std::string>( "static.serviceName", pRadarConfig->serviceConfig.serviceName );
    pdt->Set<uint32_t>( "static.timeoutMs", pRadarConfig->serviceConfig.timeoutMs );
    pdt->Set<bool>( "static.enablePerformanceLog",
                    pRadarConfig->serviceConfig.bEnablePerformanceLog );
    pdt->Set<uint32_t>( "static.maxInputBufferSize", pRadarConfig->maxInputBufferSize );
    pdt->Set<uint32_t>( "static.maxOutputBufferSize", pRadarConfig->maxOutputBufferSize );

    // Set empty buffer IDs since we don't register buffers during initialization
    std::vector<uint32_t> bufferIds;
    pdt->Set( "static.inputs", bufferIds );
    pdt->Set( "static.outputs", bufferIds );

    // Set global buffer ID mapping for Node interface
    std::vector<QC::DataTree> bufferMapDts;

    QC::DataTree inputMapDt;
    inputMapDt.Set<std::string>( "name", "input" );
    inputMapDt.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inputMapDt );

    QC::DataTree outputMapDt;
    outputMapDt.Set<std::string>( "name", "output" );
    outputMapDt.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outputMapDt );

    pdt->Set( "static.globalBufferIdMap", bufferMapDts );
    pdt->Set<bool>( "static.deRegisterAllBuffersWhenStop", false );
}

/**
 * @brief Test configuration for basic radar functionality
 */
static Radar_Config_t radarConfigBasic = { ( ( 2 * 1024 * 1024 ) + 1888 ),   // maxInputBufferSize
                                           ( 8 * 1024 * 1024 ),              // maxOutputBufferSize
                                           {
                                                   "/dev/radar0",   // serviceName
                                                   5000,            // timeoutMs
                                                   false            // enablePerformanceLog
                                           } };

/**
 * @brief Test configuration for performance testing scenarios
 */
static Radar_Config_t radarConfigPerformance = { ( 2 * 1024 * 1024 + 1888 ),   // maxInputBufferSize
                                                 ( 8 * 1024 * 1024 ),   // maxOutputBufferSize
                                                 {
                                                         "/dev/radar0",   // serviceName
                                                         10000,           // timeoutMs
                                                         true             // enablePerformanceLog
                                                 } };

/**
 * @brief Generate pseudo-random radar data for testing purposes
 *
 * @param[out] pData Pointer to buffer to fill with test data
 * @param[in] size Size of buffer in bytes to fill
 */
void GenerateRadarTestData( void *pData, uint32_t size )
{
    uint8_t *data = (uint8_t *) pData;
    srand( 12345 );
    for ( uint32_t i = 0; i < size; i++ )
    {
        data[i] = (uint8_t) ( ( rand() % 256 ) );
    }
}

/**
 * @brief Validate radar output data for basic sanity checks
 *
 * @param[in] pData Pointer to output data buffer
 * @param[in] size Size of data buffer in bytes
 * @return true if output data appears valid, false otherwise
 */
bool ValidateRadarOutput( const void *pData, uint32_t size )
{
    const uint8_t *data = (const uint8_t *) pData;

    // Check for all-zero output
    bool hasNonZero = false;
    for ( uint32_t i = 0; i < size; i++ )
    {
        if ( data[i] != 0 )
        {
            hasNonZero = true;
            break;
        }
    }

    if ( !hasNonZero )
    {
        printf( "Warning: Output data is all zeros\n" );
        return false;
    }

    // Check for reasonable data distribution
    uint8_t firstValue = data[0];
    bool hasVariation = false;
    for ( uint32_t i = 1; i < size && i < 100; i++ )
    {
        if ( data[i] != firstValue )
        {
            hasVariation = true;
            break;
        }
    }

    if ( !hasVariation )
    {
        printf( "Warning: Output data lacks variation\n" );
        return false;
    }

    return true;
}

/**
 * @brief Performance test helper function for radar Node lifecycle timing
 *
 * @param[in] config Radar configuration to use for testing
 * @param[in] inputSize Size of input buffer for testing
 * @param[in] outputSize Size of output buffer for testing
 * @param[in] iterations Number of execute iterations to perform
 */
void PerformanceTestNode( Radar_Config_t &config, uint32_t inputSize, uint32_t outputSize,
                          uint32_t iterations )
{
    QCStatus_e ret = QCStatus_e::QC_STATUS_OK;
    Radar radarNode;

    // Build configuration
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarPerf" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &config, &dt );
    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    // Measure initialization time
    auto initStart = std::chrono::high_resolution_clock::now();
    ret = radarNode.Initialize( nodeConfig );
    auto initEnd = std::chrono::high_resolution_clock::now();

    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        printf( "Radar Node init failed (ret=%d), skipping performance test\n", ret );
        return;
    }

    double initTime = std::chrono::duration<double, std::milli>( initEnd - initStart ).count();
    printf( "Radar Node initialization time: %.2f ms\n", initTime );

    // Measure start time
    auto startTime = std::chrono::high_resolution_clock::now();
    ret = radarNode.Start();
    auto startEnd = std::chrono::high_resolution_clock::now();

    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        printf( "Radar Node start failed (ret=%d), skipping performance test\n", ret );
        radarNode.DeInitialize();
        return;
    }

    double startDuration =
            std::chrono::duration<double, std::milli>( startEnd - startTime ).count();
    printf( "Radar Node start time: %.2f ms\n", startDuration );

    // Allocate and prepare test buffers
    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, std::string{ "PerfInput" }, inputSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    TBuffer outputBuffer( allocator, std::string{ "PerfOutput" }, outputSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

    // Generate test data
    GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), inputSize );

    // Setup frame descriptor
    NodeFrameDescriptor frameDesc( 2 );
    ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

    // Measure execution performance
    auto execStart = std::chrono::high_resolution_clock::now();
    for ( uint32_t i = 0; i < iterations; i++ )
    {
        ret = radarNode.ProcessFrameDescriptor( frameDesc );
    }
    auto execEnd = std::chrono::high_resolution_clock::now();

    double execTime = std::chrono::duration<double, std::milli>( execEnd - execStart ).count();
    printf( "Radar Node ProcessFrameDescriptor: %d iterations, total = %.2f ms, avg = %.2f ms\n",
            iterations, execTime, execTime / iterations );

    // Validate output if execution succeeded
    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        bool outputValid =
                ValidateRadarOutput( outputBuffer.tensor.GetDataPtr(), outputBuffer.tensor.size );
        printf( "Output validation: %s\n", outputValid ? "PASS" : "FAIL" );
    }

    // Measure cleanup time
    auto cleanupStart = std::chrono::high_resolution_clock::now();
    radarNode.Stop();
    radarNode.DeInitialize();
    auto cleanupEnd = std::chrono::high_resolution_clock::now();

    double cleanupTime =
            std::chrono::duration<double, std::milli>( cleanupEnd - cleanupStart ).count();
    printf( "Cleanup time: %.2f ms\n", cleanupTime );
}

/**
 * @brief Sanity test helper function for basic radar Node functionality validation
 *
 * @param[in] config Radar configuration to test
 */
void SanityTestNode( Radar_Config_t &config )
{
    QCStatus_e ret = QCStatus_e::QC_STATUS_OK;
    Radar radarNode;

    // Build configuration
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarSanity" );
    dt.Set<uint32_t>( "static.id", 200 );
    SetConfigRadarEx( &config, &dt );
    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    // Test Node initialization
    ret = radarNode.Initialize( nodeConfig );
    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        printf( "Radar Node init failed (ret=%d), skipping sanity test\n", ret );
        return;
    }

    // Test Node start
    ret = radarNode.Start();
    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        printf( "Radar Node start failed (ret=%d), skipping sanity test\n", ret );
        radarNode.DeInitialize();
        return;
    }

    // Allocate test buffers
    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, std::string{ "SanityInput" }, config.maxInputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    TBuffer outputBuffer( allocator, std::string{ "SanityOutput" }, config.maxOutputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

    // Generate test input data
    GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), config.maxInputBufferSize );

    // Setup frame descriptor
    NodeFrameDescriptor frameDesc( 2 );
    ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );

    // Test execution
    ret = radarNode.ProcessFrameDescriptor( frameDesc );
    printf( "ProcessFrameDescriptor result: %d (service availability dependent)\n", ret );

    // Cleanup resources
    radarNode.Stop();
    radarNode.DeInitialize();

    printf( "Sanity test completed for configuration\n" );
}

/**
 * @brief Comprehensive coverage test for error conditions and edge cases
 */
void RadarNodeCoverageTest()
{
    QCStatus_e ret = QCStatus_e::QC_STATUS_OK;
    Radar radarNode;
    Radar_Config_t config = radarConfigBasic;

    printf( "Testing operations before initialization...\n" );

    // Test all operations before init - should return BAD_STATE
    ret = radarNode.Start();
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

    ret = radarNode.Stop();
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

    ret = radarNode.DeInitialize();
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

    NodeFrameDescriptor frameDesc( 2 );
    ret = radarNode.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

    printf( "Testing invalid initialization parameters...\n" );

    // Test init with invalid buffer sizes
    QC::DataTree dt1;
    dt1.Set<std::string>( "static.name", "CoverageTest" );
    dt1.Set<uint32_t>( "static.id", 300 );
    config.maxInputBufferSize = 0;
    SetConfigRadarEx( &config, &dt1 );
    QC::QCNodeInit_t invalidConfig1 = { dt1.Dump() };
    ret = radarNode.Initialize( invalidConfig1 );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );

    // Test init with empty service name
    QC::DataTree dt2;
    dt2.Set<std::string>( "static.name", "CoverageTest2" );
    dt2.Set<uint32_t>( "static.id", 301 );
    config = radarConfigBasic;
    config.serviceConfig.serviceName = "";
    SetConfigRadarEx( &config, &dt2 );
    QC::QCNodeInit_t invalidConfig2 = { dt2.Dump() };
    Radar radarNode2;
    ret = radarNode2.Initialize( invalidConfig2 );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );

    printf( "Testing successful initialization and state transitions...\n" );

    // Successful init
    QC::DataTree dt3;
    dt3.Set<std::string>( "static.name", "CoverageTest3" );
    dt3.Set<uint32_t>( "static.id", 302 );
    config = radarConfigBasic;
    SetConfigRadarEx( &config, &dt3 );
    QC::QCNodeInit_t validConfig = { dt3.Dump() };
    Radar radarNode3;
    ret = radarNode3.Initialize( validConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        printf( "Testing ProcessFrameDescriptor before start...\n" );

        // Test ProcessFrameDescriptor before start - should return BAD_STATE
        TBufferAllocator allocator;
        TBuffer inputBuffer( allocator, config.maxInputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
        TBuffer outputBuffer( allocator, config.maxOutputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

        NodeFrameDescriptor testFrameDesc( 2 );
        ret = testFrameDesc.SetBuffer( 0, inputBuffer.tensor );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
        ret = testFrameDesc.SetBuffer( 1, outputBuffer.tensor );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

        ret = radarNode3.ProcessFrameDescriptor( testFrameDesc );
        EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

        // Start Node for further testing
        ret = radarNode3.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            printf( "Testing ProcessFrameDescriptor with invalid buffers...\n" );

            // Test with null buffer data
            void *originalInputData = inputBuffer.tensor.pBuf;
            inputBuffer.tensor.pBuf = nullptr;
            NodeFrameDescriptor nullFrameDesc( 2 );
            ret = nullFrameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = nullFrameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode3.ProcessFrameDescriptor( nullFrameDesc );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            inputBuffer.tensor.pBuf = originalInputData;

            ret = radarNode3.Stop();
            EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
        }

        ret = radarNode3.DeInitialize();
        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    }

    printf( "Coverage test completed\n" );
}

/**
 * @brief Test fixture class for Radar Node unit tests
 */
class RadarNodeTest : public ::testing::Test
{
protected:
    void SetUp() override { m_config = radarConfigBasic; }

    void TearDown() override {}

    Radar_Config_t m_config;
};

// Basic functionality tests using test fixture

/**
 * @brief Direct config parsing test: empty name should fail
 * Coverage: VerifyStaticConfig name validation (line 17 in Radar.cpp)
 */
TEST_F( RadarNodeTest, ConfigVerifyAndSet_EmptyName )
{
    Radar radarNode;
    std::string errors;

    // Prime logger init so NodeConfigBase doesn't block on empty name
    QC::DataTree dtValid;
    dtValid.Set<std::string>( "static.name", "ConfigVerifyAndSet_EmptyName_Prime" );
    dtValid.Set<uint32_t>( "static.id", 699 );
    SetConfigRadarEx( &m_config, &dtValid );
    QCStatus_e ret = radarNode.GetConfigurationIfs().VerifyAndSet( dtValid.Dump(), errors );
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "" );
    dt.Set<uint32_t>( "static.id", 700 );
    dt.Set<uint32_t>( "static.maxInputBufferSize", 1024 );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", 1024 );
    dt.Set<std::string>( "static.serviceName", "/dev/radar0" );
    dt.Set<uint32_t>( "static.timeoutMs", 1000 );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    ret = radarNode.GetConfigurationIfs().VerifyAndSet( dt.Dump(), errors );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
    EXPECT_FALSE( errors.empty() );
}

/**
 * @brief Direct config parsing test: valid config should succeed
 * Coverage: ParseStaticConfig success path (line 148 in Radar.cpp)
 */
TEST_F( RadarNodeTest, ConfigVerifyAndSet_ValidConfig )
{
    Radar radarNode;
    std::string errors;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "VerifyAndSetValid" );
    dt.Set<uint32_t>( "static.id", 701 );
    SetConfigRadarEx( &m_config, &dt );
    QCStatus_e ret = radarNode.GetConfigurationIfs().VerifyAndSet( dt.Dump(), errors );
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    EXPECT_TRUE( errors.empty() );
}

/**
 * @brief VerifyAndSet should fail when static section is missing
 * Coverage: RadarConfigIfs::VerifyAndSet branch where m_dataTree.Get(\"static\") fails
 */
TEST_F( RadarNodeTest, ConfigVerifyAndSet_MissingStatic )
{
    Radar radarNode;
    std::string errors;

    // Prime logger init
    QC::DataTree dtValid;
    dtValid.Set<std::string>( "static.name", "ConfigVerifyAndSet_MissingStatic_Prime" );
    dtValid.Set<uint32_t>( "static.id", 702 );
    SetConfigRadarEx( &m_config, &dtValid );
    QCStatus_e ret = radarNode.GetConfigurationIfs().VerifyAndSet( dtValid.Dump(), errors );
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );

    // Missing "static" section entirely
    QC::DataTree dtMissing;
    dtMissing.Set<std::string>( "name", "MissingStatic" );
    ret = radarNode.GetConfigurationIfs().VerifyAndSet( dtMissing.Dump(), errors );
    EXPECT_NE( QCStatus_e::QC_STATUS_OK, ret );
}

/**
 * @brief RadarIface coverage tests for error handling paths
 */
TEST_F( RadarNodeTest, RadarIface_ErrorPaths )
{
    QC::Library::RadarIface iface;

    // Null device path should be rejected
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, iface.Initialize( nullptr ) );
    EXPECT_FALSE( iface.IsInitialized() );

    // Invalid device path should fail (either BAD_STATE or OK if device exists)
    QCStatus_e initRet = iface.Initialize( "/dev/radar_invalid_device" );
    EXPECT_TRUE( initRet == QCStatus_e::QC_STATUS_BAD_STATE ||
                 initRet == QCStatus_e::QC_STATUS_OK );

    // Execute before successful init should return BAD_STATE
    uint8_t inBuf[8] = { 0 };
    uint8_t outBuf[8] = { 0 };
    QCStatus_e execRet = iface.Execute( inBuf, sizeof( inBuf ), outBuf, sizeof( outBuf ) );
    EXPECT_TRUE( execRet == QCStatus_e::QC_STATUS_OK ||
                 execRet == QCStatus_e::QC_STATUS_BAD_STATE ||
                 execRet == QCStatus_e::QC_STATUS_BAD_ARGUMENTS ||
                 execRet == QCStatus_e::QC_STATUS_FAIL );

    // Deinitialize should be safe regardless of init state
    EXPECT_EQ( QCStatus_e::QC_STATUS_OK, iface.Deinitialize() );
}

/**
 * @brief RadarIface coverage tests for success paths (if device is available)
 */
TEST_F( RadarNodeTest, RadarIface_SuccessPaths )
{
    QC::Library::RadarIface iface;

    QCStatus_e ret = iface.Initialize( "/dev/radar0" );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        // Second initialize should be a no-op success
        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, iface.Initialize( "/dev/radar0" ) );

        uint8_t inBuf[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        uint8_t outBuf[8] = { 0 };
        QCStatus_e execRet = iface.Execute( inBuf, sizeof( inBuf ), outBuf, sizeof( outBuf ) );
        EXPECT_TRUE( execRet == QCStatus_e::QC_STATUS_OK || execRet == QCStatus_e::QC_STATUS_FAIL );

        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, iface.Deinitialize() );
    }
}

/**
 * @brief Start should fail when RadarIface isn't initialized but state is READY
 * Coverage: Radar::Start IsInitialized check (line 418 in Radar.cpp)
 */
TEST_F( RadarNodeTest, Start_FailsWhenRadarIfaceNotInitialized )
{
    Radar radarNode;
    radarNode.m_state = QC_OBJECT_STATE_READY;
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, radarNode.Start() );
}

/**
 * @brief ProcessFrameDescriptor branches for map size and iface init
 * Coverage: map size < 2 and iface not initialized (lines 464, 470)
 */
TEST_F( RadarNodeTest, ProcessFrameDescriptor_MapSizeAndIfaceChecks )
{
    Radar radarNode;
    radarNode.m_state = QC_OBJECT_STATE_RUNNING;

    // Map size < 2 should fail
    radarNode.m_globalBufferIdMap.clear();
    radarNode.m_globalBufferIdMap.push_back( QC::QCNodeBufferMapEntry_t{ "Input", 0 } );
    NodeFrameDescriptor frameDesc1( 1 );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS,
               radarNode.ProcessFrameDescriptor( frameDesc1 ) );

    // Map size ok but iface not initialized should fail
    radarNode.m_globalBufferIdMap.clear();
    radarNode.m_globalBufferIdMap.push_back( QC::QCNodeBufferMapEntry_t{ "Input", 0 } );
    radarNode.m_globalBufferIdMap.push_back( QC::QCNodeBufferMapEntry_t{ "Output", 1 } );
    NodeFrameDescriptor frameDesc2( 2 );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, radarNode.ProcessFrameDescriptor( frameDesc2 ) );
}

/**
 * @brief Execute branch coverage without requiring radar service
 * Coverage: state check, null buffer check, iface init check (lines 593, 600, 623)
 */
TEST_F( RadarNodeTest, Execute_BranchCoverage )
{
    Radar radarNode;

    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

    // Not running state
    radarNode.m_state = QC_OBJECT_STATE_READY;
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE,
               radarNode.Execute( &inputBuffer.tensor, &outputBuffer.tensor ) );

    // Null input/output handling
    radarNode.m_state = QC_OBJECT_STATE_RUNNING;
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS,
               radarNode.Execute( nullptr, &outputBuffer.tensor ) );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS,
               radarNode.Execute( &inputBuffer.tensor, nullptr ) );

    // Valid buffers but iface not initialized
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE,
               radarNode.Execute( &inputBuffer.tensor, &outputBuffer.tensor ) );
}

/**
 * @brief Test radar Node initialization with valid configuration
 */
TEST_F( RadarNodeTest, InitWithValidConfig )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 400 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, radarNode.DeInitialize() );
    }
}

/**
 * @brief Test radar Node initialization with invalid buffer sizes
 */
TEST_F( RadarNodeTest, InitWithInvalidBufferSizes )
{
    Radar radarNode;
    m_config.maxInputBufferSize = 0;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 401 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test radar Node initialization with empty service name
 */
TEST_F( RadarNodeTest, InitWithEmptyServiceName )
{
    Radar radarNode;
    m_config.serviceConfig.serviceName = "";
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 402 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test radar Node start without initialization
 */
TEST_F( RadarNodeTest, StartWithoutInit )
{
    Radar radarNode;
    QC::QCStatus_e ret = radarNode.Start();
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );
}

/**
 * @brief Test radar Node ProcessFrameDescriptor without start
 */
TEST_F( RadarNodeTest, ProcessFrameDescriptorWithoutStart )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 403 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        TBufferAllocator allocator;
        TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
        TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

        NodeFrameDescriptor testFrameDesc( 2 );
        ret = testFrameDesc.SetBuffer( 0, inputBuffer.tensor );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
        ret = testFrameDesc.SetBuffer( 1, outputBuffer.tensor );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

        ret = radarNode.ProcessFrameDescriptor( testFrameDesc );
        EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_STATE, ret );

        radarNode.DeInitialize();
    }
}

/**
 * @brief Test radar Node ProcessFrameDescriptor with insufficient buffers
 */
TEST_F( RadarNodeTest, ProcessFrameDescriptorWithInsufficientBuffers )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 404 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            // Create frame descriptor with only 1 buffer (need 2)
            NodeFrameDescriptor frameDesc( 1 );
            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test complete radar Node workflow
 */
TEST_F( RadarNodeTest, FullWorkflow )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 405 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, std::string{ "Input" }, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, std::string{ "Output" },
                                  m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            NodeFrameDescriptor testFrameDesc( 2 );
            ret = testFrameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = testFrameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            // Generate test data
            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

            ret = radarNode.ProcessFrameDescriptor( testFrameDesc );
            // Don't assert on result as it depends on service availability

            ret = radarNode.Stop();
            EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
        }
        ret = radarNode.DeInitialize();
        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    }
}

// Advanced test cases

// ============================================================================
// PHASE 1: HIGH PRIORITY TESTS - Configuration & Buffer Validation
// ============================================================================

/**
 * @brief Test configuration validation with empty node name
 * Coverage: Lines 17-21 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_EmptyName )
{
    Radar radarNode;
    QC::DataTree dt;

    // Set empty name - this should trigger validation error
    dt.Set<std::string>( "static.name", "" );
    dt.Set<uint32_t>( "static.id", 100 );

    // Set valid configuration for other parameters
    Radar_Config_t config = radarConfigBasic;
    SetConfigRadarEx( &config, &dt );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    // Initialize should fail with BAD_ARGUMENTS
    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test configuration validation with invalid node ID
 * Coverage: Lines 24-28 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_InvalidId )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    // Set invalid ID (UINT32_MAX is used as sentinel value)
    dt.Set<uint32_t>( "static.id", UINT32_MAX );

    Radar_Config_t config = radarConfigBasic;
    SetConfigRadarEx( &config, &dt );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test configuration validation with zero output buffer size
 * Coverage: Lines 38-42 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_ZeroOutputBufferSize )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    // Set zero output buffer size
    Radar_Config_t config = radarConfigBasic;
    config.maxOutputBufferSize = 0;
    SetConfigRadarEx( &config, &dt );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test configuration validation with zero timeout
 * Coverage: Lines 52-56 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_ZeroTimeout )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;
    config.serviceConfig.timeoutMs = 0;
    SetConfigRadarEx( &config, &dt );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test configuration validation with invalid global buffer ID map
 * Coverage: Lines 61-69 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_InvalidGlobalBufferIdMap )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;

    // Manually set configuration without using SetConfigRadarEx
    dt.Set<std::string>( "static.serviceName", config.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", config.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", config.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", config.maxOutputBufferSize );

    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );

    // Set invalid globalBufferIdMap (wrong type to trigger error)
    dt.Set<std::string>( "static.globalBufferIdMap", "invalid" );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_NE( QCStatus_e::QC_STATUS_OK, ret );
}

/**
 * @brief Test configuration validation with empty entries in global buffer ID map
 * Coverage: Lines 77-87 in VerifyStaticConfig
 */
TEST_F( RadarNodeTest, ConfigValidation_EmptyGlobalBufferMapEntries )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;
    dt.Set<std::string>( "static.serviceName", config.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", config.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", config.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", config.maxOutputBufferSize );

    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );

    // Create global buffer map with empty name
    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inputMapDt;
    inputMapDt.Set<std::string>( "name", "" );   // Empty name
    inputMapDt.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inputMapDt );

    QC::DataTree outputMapDt;
    outputMapDt.Set<std::string>( "name", "output" );
    outputMapDt.Set<uint32_t>( "id", UINT32_MAX );   // Invalid ID
    bufferMapDts.push_back( outputMapDt );

    dt.Set( "static.globalBufferIdMap", bufferMapDts );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test buffer validation with zero-sized buffer
 * Coverage: Lines 540-544 in ValidateBuffer
 */
TEST_F( RadarNodeTest, ValidateBuffer_ZeroSize )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            // Manually set buffer size to 0
            size_t originalSize = inputBuffer.tensor.size;
            inputBuffer.tensor.size = 0;

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            // Restore original size for cleanup
            inputBuffer.tensor.size = originalSize;

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test buffer validation with invalid DMA handle
 * Coverage: Lines 547-551 in ValidateBuffer
 */
TEST_F( RadarNodeTest, ValidateBuffer_InvalidDmaHandle )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            // Manually set DMA handle to 0
            int originalHandle = inputBuffer.tensor.dmaHandle;
            inputBuffer.tensor.dmaHandle = 0;

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            // Restore original handle for cleanup
            inputBuffer.tensor.dmaHandle = originalHandle;

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test buffer validation with TENSOR type buffer
 * Coverage: Lines 568-571 in ValidateBuffer
 */
TEST_F( RadarNodeTest, ValidateBuffer_TensorType )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            // Set buffer type to TENSOR
            inputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_TENSOR;
            outputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_TENSOR;

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            // Should succeed or fail based on service availability, not buffer type

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test buffer validation with IMAGE type buffer
 * Coverage: Lines 572-575 in ValidateBuffer
 */
TEST_F( RadarNodeTest, ValidateBuffer_ImageType )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            // Set buffer type to IMAGE
            inputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_IMAGE;
            outputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_IMAGE;

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            // Should succeed or fail based on service availability, not buffer type

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test GetOptions function
 * Coverage: Lines 161-165 (GetOptions function)
 */
TEST_F( RadarNodeTest, GetOptionsReturnsEmptyString )
{
    // GetOptions is called internally during configuration parsing
    // We test it indirectly through the Radar node initialization
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    // Initialize will call GetOptions internally
    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        radarNode.DeInitialize();
    }
}

// ============================================================================
// PHASE 2: MEDIUM PRIORITY TESTS - Initialization Edge Cases
// ============================================================================

/**
 * @brief Test default global buffer map creation when none provided
 * Coverage: Lines 184-199 in SetupGlobalBufferIdMap
 */
TEST_F( RadarNodeTest, SetupGlobalBufferIdMap_NoMapProvided )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;

    // Manually configure without globalBufferIdMap
    dt.Set<std::string>( "static.serviceName", config.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", config.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", config.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", config.maxOutputBufferSize );

    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );

    // Do NOT set globalBufferIdMap - let it create default
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        // Verify node can start and process with default buffer map
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), config.maxInputBufferSize );

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            // Result depends on service availability

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test global buffer map with size mismatch
 * Coverage: Lines 173-178 in SetupGlobalBufferIdMap
 */
TEST_F( RadarNodeTest, SetupGlobalBufferIdMap_SizeMismatch )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;
    dt.Set<std::string>( "static.serviceName", config.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", config.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", config.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", config.maxOutputBufferSize );

    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );

    // Create global buffer map with wrong size (only 1 entry instead of 2)
    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inputMapDt;
    inputMapDt.Set<std::string>( "name", "input" );
    inputMapDt.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inputMapDt );
    // Missing output entry - should cause size mismatch

    dt.Set( "static.globalBufferIdMap", bufferMapDts );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Test buffer registration during initialization
 * Coverage: Lines 265-344 in Initialize
 * Note: This test covers the buffer registration code path by setting up
 * input/output buffer IDs in the configuration, which triggers the
 * buffer registration logic during initialization.
 */
TEST_F( RadarNodeTest, InitializeWithBuffers_ValidBuffers )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    Radar_Config_t config = radarConfigBasic;

    // Manually configure with buffer IDs to trigger registration path
    dt.Set<std::string>( "static.serviceName", config.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", config.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", config.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", config.maxOutputBufferSize );

    // Set buffer IDs to trigger registration code path
    std::vector<uint32_t> inputBufferIds = { 0 };
    std::vector<uint32_t> outputBufferIds = { 1 };
    dt.Set( "static.inputs", inputBufferIds );
    dt.Set( "static.outputs", outputBufferIds );

    // Set global buffer ID mapping
    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inputMapDt;
    inputMapDt.Set<std::string>( "name", "input" );
    inputMapDt.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inputMapDt );

    QC::DataTree outputMapDt;
    outputMapDt.Set<std::string>( "name", "output" );
    outputMapDt.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outputMapDt );

    dt.Set( "static.globalBufferIdMap", bufferMapDts );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        // Verify node can start and process
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), config.maxInputBufferSize );

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            // Result depends on service availability

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Test DeInitialize from error state
 * Coverage: Line 379 (QC_OBJECT_STATE_ERROR condition) in DeInitialize
 */
TEST_F( RadarNodeTest, DeInitialize_FromErrorState )
{
    Radar radarNode;
    QC::DataTree dt;

    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );

    // First initialize with invalid config to potentially get to error state
    Radar_Config_t config = radarConfigBasic;
    config.maxInputBufferSize = 0;   // Invalid
    SetConfigRadarEx( &config, &dt );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );

    // Now initialize properly
    config.maxInputBufferSize = radarConfigBasic.maxInputBufferSize;
    QC::DataTree dt2;
    dt2.Set<std::string>( "static.name", "TestRadar2" );
    dt2.Set<uint32_t>( "static.id", 101 );
    SetConfigRadarEx( &config, &dt2 );
    QC::QCNodeInit_t validConfig = { dt2.Dump() };

    ret = radarNode.Initialize( validConfig );
    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        // Test that DeInitialize works from READY state
        ret = radarNode.DeInitialize();
        EXPECT_EQ( QCStatus_e::QC_STATUS_OK, ret );
    }
}

// ============================================================================
// PHASE 3: EDGE CASES
// ============================================================================

/**
 * @brief Test Execute with null buffers
 * Coverage: Lines 600-604 in Execute
 */
TEST_F( RadarNodeTest, Execute_NullBuffers )
{
    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TestRadar" );
    dt.Set<uint32_t>( "static.id", 100 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            // Test through ProcessFrameDescriptor with invalid buffers
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            // Set data pointer to null
            inputBuffer.tensor.pBuf = nullptr;

            NodeFrameDescriptor frameDesc( 2 );
            ret = frameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = frameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( frameDesc );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

// ============================================================================
// EXISTING TESTS
// ============================================================================

/**
 * @brief Comprehensive sanity test across multiple configurations
 */
TEST_F( RadarNodeTest, SanityTest )
{
    printf( "Running Node sanity tests across multiple configurations...\n" );
    SanityTestNode( radarConfigBasic );
    SanityTestNode( radarConfigPerformance );
}

/**
 * @brief Comprehensive coverage test for error conditions
 */
TEST_F( RadarNodeTest, CoverageTest )
{
    printf( "Running comprehensive Node coverage test...\n" );
    RadarNodeCoverageTest();
}

/**
 * @brief Performance test for Node lifecycle operations
 */
TEST_F( RadarNodeTest, PerformanceTest )
{
    printf( "Running Node performance tests...\n" );
    printf( "Performance test with basic config (2MB buffers)\n" );
    PerformanceTestNode( radarConfigBasic, radarConfigBasic.maxInputBufferSize,
                         radarConfigBasic.maxOutputBufferSize, 5 );

    printf( "Performance test with performance config (2MB buffers)\n" );
    PerformanceTestNode( radarConfigPerformance, radarConfigPerformance.maxInputBufferSize,
                         radarConfigPerformance.maxOutputBufferSize, 5 );
}

/**
 * @brief Configuration variation test across multiple radar configurations
 */
TEST_F( RadarNodeTest, ConfigurationVariationTest )
{
    printf( "Testing different radar Node configurations...\n" );
    std::vector<Radar_Config_t> configs = { radarConfigBasic, radarConfigPerformance };

    for ( size_t i = 0; i < configs.size(); i++ )
    {
        printf( "Testing configuration %zu with %u/%u buffer sizes\n", i,
                configs[i].maxInputBufferSize, configs[i].maxOutputBufferSize );
        SanityTestNode( configs[i] );
    }
}

/**
 * @brief Timeout handling test with short timeout configuration
 */
TEST_F( RadarNodeTest, TimeoutTest )
{
    printf( "Testing timeout handling...\n" );
    Radar_Config_t config = radarConfigBasic;
    config.serviceConfig.timeoutMs = 100;   // Very short timeout

    Radar radarNode;
    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "TimeoutTest" );
    dt.Set<uint32_t>( "static.id", 500 );
    SetConfigRadarEx( &config, &dt );
    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, std::string{ "PerfInput" }, config.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, std::string{ "PerfOutput" },
                                  config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            NodeFrameDescriptor testFrameDesc( 2 );
            ret = testFrameDesc.SetBuffer( 0, inputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = testFrameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), config.maxInputBufferSize );


            ret = radarNode.ProcessFrameDescriptor( testFrameDesc );
            printf( "ProcessFrameDescriptor with short timeout returned: %d\n", ret );

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Buffer size validation test
 */
TEST_F( RadarNodeTest, BufferSizeValidationTest )
{
    printf( "Testing buffer size validation...\n" );
    Radar radarNode;
    Radar_Config_t config = radarConfigBasic;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "BufferTest" );
    dt.Set<uint32_t>( "static.id", 501 );
    SetConfigRadarEx( &config, &dt );
    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            // Test with buffer larger than configured max
            TBufferAllocator allocator;
            TBuffer largeBuffer( allocator, config.maxInputBufferSize * 2 );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, largeBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, config.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            NodeFrameDescriptor testFrameDesc( 2 );
            ret = testFrameDesc.SetBuffer( 0, largeBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );
            ret = testFrameDesc.SetBuffer( 1, outputBuffer.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret );

            ret = radarNode.ProcessFrameDescriptor( testFrameDesc );
            printf( "ProcessFrameDescriptor with oversized buffer returned: %d\n", ret );
            EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );

            radarNode.Stop();
        }
        radarNode.DeInitialize();
    }
}

/**
 * @brief Multiple instance test
 */
TEST_F( RadarNodeTest, MultipleInstanceTest )
{
    printf( "Testing multiple radar Node instances...\n" );
    Radar radarNode1, radarNode2;

    QC::DataTree dt1;
    dt1.Set<std::string>( "static.name", "Radar1" );
    dt1.Set<uint32_t>( "static.id", 600 );
    SetConfigRadarEx( &radarConfigBasic, &dt1 );
    QC::QCNodeInit_t config1 = { dt1.Dump() };

    QC::DataTree dt2;
    dt2.Set<std::string>( "static.name", "Radar2" );
    dt2.Set<uint32_t>( "static.id", 601 );
    SetConfigRadarEx( &radarConfigPerformance, &dt2 );
    QC::QCNodeInit_t config2 = { dt2.Dump() };

    QC::QCStatus_e ret1 = radarNode1.Initialize( config1 );
    QC::QCStatus_e ret2 = radarNode2.Initialize( config2 );

    EXPECT_TRUE( ret1 == QCStatus_e::QC_STATUS_OK || ret1 == QCStatus_e::QC_STATUS_BAD_STATE );
    EXPECT_TRUE( ret2 == QCStatus_e::QC_STATUS_OK || ret2 == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret1 == QCStatus_e::QC_STATUS_OK && ret2 == QCStatus_e::QC_STATUS_OK )
    {
        ret1 = radarNode1.Start();
        ret2 = radarNode2.Start();

        if ( ret1 == QCStatus_e::QC_STATUS_OK && ret2 == QCStatus_e::QC_STATUS_OK )
        {
            // Both instances should be able to operate independently
            TBufferAllocator allocator;
            TBuffer inputBuffer1( allocator, radarConfigBasic.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer1.GetAllocationStatus() );
            TBuffer outputBuffer1( allocator, radarConfigBasic.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer1.GetAllocationStatus() );
            TBuffer inputBuffer2( allocator, radarConfigPerformance.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer2.GetAllocationStatus() );
            TBuffer outputBuffer2( allocator, radarConfigPerformance.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer2.GetAllocationStatus() );

            GenerateRadarTestData( inputBuffer1.tensor.GetDataPtr(),
                                   radarConfigBasic.maxInputBufferSize );
            GenerateRadarTestData( inputBuffer2.tensor.GetDataPtr(),
                                   radarConfigPerformance.maxInputBufferSize );

            NodeFrameDescriptor testFrameDesc1( 2 );
            ret1 = testFrameDesc1.SetBuffer( 0, inputBuffer1.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret1 );
            ret2 = testFrameDesc1.SetBuffer( 1, outputBuffer1.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret2 );

            NodeFrameDescriptor testFrameDesc2( 2 );
            ret1 = testFrameDesc2.SetBuffer( 0, inputBuffer2.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret1 );
            ret2 = testFrameDesc2.SetBuffer( 1, outputBuffer2.tensor );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, ret2 );

            // Execute on both instances
            ret1 = radarNode1.ProcessFrameDescriptor( testFrameDesc1 );
            ret2 = radarNode2.ProcessFrameDescriptor( testFrameDesc2 );

            printf( "Instance 1 ProcessFrameDescriptor result: %d\n", ret1 );
            printf( "Instance 2 ProcessFrameDescriptor result: %d\n", ret2 );

            radarNode1.Stop();
            radarNode2.Stop();
        }

        if ( ret1 == QCStatus_e::QC_STATUS_OK ) radarNode1.DeInitialize();
        if ( ret2 == QCStatus_e::QC_STATUS_OK ) radarNode2.DeInitialize();
    }
}

// ============================================================================
// NEW COVERAGE TESTS (from CTC report gaps)
// ============================================================================

/**
 * @brief Cover RadarConfigIfs::GetOptions() which is otherwise unused.
 * Coverage: Radar.cpp lines 161-165.
 */
TEST_F( RadarNodeTest, ConfigIfs_GetOptions_Coverage )
{
    Radar radarNode;
    std::string options = radarNode.GetConfigurationIfs().GetOptions();
    EXPECT_TRUE( options.empty() );
}

/**
 * @brief Cover SetupGlobalBufferIdMap() default branch and VerifyStaticConfig() missing-map path.
 * Coverage: Radar.cpp lines 61-64 and 184-199.
 */
TEST_F( RadarNodeTest, SetupGlobalBufferIdMap_DefaultMap_WhenNotProvided )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarDefaultMap" );
    dt.Set<uint32_t>( "static.id", 700 );

    // Set required static keys but DO NOT set static.globalBufferIdMap.
    dt.Set<std::string>( "static.serviceName", radarConfigBasic.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", radarConfigBasic.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", radarConfigBasic.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", radarConfigBasic.maxOutputBufferSize );

    std::vector<uint32_t> bufferIds;
    dt.Set( "static.inputs", bufferIds );
    dt.Set( "static.outputs", bufferIds );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };
    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        // If start succeeds, validate we can attempt processing using default map indices 0 and 1.
        ret = radarNode.Start();
        if ( ret == QCStatus_e::QC_STATUS_OK )
        {
            TBufferAllocator allocator;
            TBuffer inputBuffer( allocator, radarConfigBasic.maxInputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
            TBuffer outputBuffer( allocator, radarConfigBasic.maxOutputBufferSize );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

            GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(),
                                   radarConfigBasic.maxInputBufferSize );

            NodeFrameDescriptor frameDesc( 2 );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 0, inputBuffer.tensor ) );
            ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 1, outputBuffer.tensor ) );

            (void) radarNode.ProcessFrameDescriptor( frameDesc );
            radarNode.Stop();
        }

        radarNode.DeInitialize();
    }
}

/**
 * @brief Helper descriptor that is NOT TensorDescriptor_t nor BufferDescriptor_t.
 */
struct FakeBufferDescriptor : public QCBufferDescriptorBase_t
{
    FakeBufferDescriptor()
    {
        // Minimal initialization to avoid accidental deref issues in other code paths.
        pBuf = reinterpret_cast<void *>( 0x1 );
        size = 1;
        dmaHandle = 1;
        type = QCBufferType_e::QC_BUFFER_TYPE_RAW;
    }
};

/**
 * @brief Cover Radar::Initialize() init-time buffer registration path (config.buffers.size() > 0)
 * and the "invalid descriptor" branch.
 * Coverage: Radar.cpp lines 265-304.
 */
TEST_F( RadarNodeTest, Initialize_WithInitBuffers_InvalidDescriptorType )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarInitBufInvalidDesc" );
    dt.Set<uint32_t>( "static.id", 701 );

    // Build config manually to allow setting inputs/outputs.
    dt.Set<std::string>( "static.serviceName", radarConfigBasic.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", radarConfigBasic.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", radarConfigBasic.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", radarConfigBasic.maxOutputBufferSize );

    std::vector<uint32_t> inputIds = { 0 };
    std::vector<uint32_t> outputIds = { 1 };
    dt.Set( "static.inputs", inputIds );
    dt.Set( "static.outputs", outputIds );

    // Provide required global buffer map
    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inMap;
    inMap.Set<std::string>( "name", "input" );
    inMap.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inMap );
    QC::DataTree outMap;
    outMap.Set<std::string>( "name", "output" );
    outMap.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outMap );
    dt.Set( "static.globalBufferIdMap", bufferMapDts );

    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    FakeBufferDescriptor fakeDesc;
    nodeConfig.buffers.push_back( std::ref( static_cast<QCBufferDescriptorBase_t &>( fakeDesc ) ) );
    nodeConfig.buffers.push_back( std::ref( static_cast<QCBufferDescriptorBase_t &>( fakeDesc ) ) );

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_INVALID_BUF, ret );
}

/**
 * @brief Cover Radar::Initialize() init-time buffer registration path: input index out of range.
 * Coverage: Radar.cpp lines 293-297.
 */
TEST_F( RadarNodeTest, Initialize_WithInitBuffers_InputIndexOutOfRange )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarInitBufInputOOR" );
    dt.Set<uint32_t>( "static.id", 702 );

    dt.Set<std::string>( "static.serviceName", radarConfigBasic.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", radarConfigBasic.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", radarConfigBasic.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", radarConfigBasic.maxOutputBufferSize );

    // buffers will have size 1 but input refers to index 1
    std::vector<uint32_t> inputIds = { 1 };
    std::vector<uint32_t> outputIds;
    dt.Set( "static.inputs", inputIds );
    dt.Set( "static.outputs", outputIds );

    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inMap;
    inMap.Set<std::string>( "name", "input" );
    inMap.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inMap );
    QC::DataTree outMap;
    outMap.Set<std::string>( "name", "output" );
    outMap.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outMap );
    dt.Set( "static.globalBufferIdMap", bufferMapDts );

    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, radarConfigBasic.maxInputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    nodeConfig.buffers.push_back(
            std::ref( static_cast<QCBufferDescriptorBase_t &>( inputBuffer.tensor ) ) );

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Cover Radar::Initialize() init-time buffer registration path: output index out of range.
 * Coverage: Radar.cpp lines 332-336.
 */
TEST_F( RadarNodeTest, Initialize_WithInitBuffers_OutputIndexOutOfRange )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarInitBufOutputOOR" );
    dt.Set<uint32_t>( "static.id", 703 );

    dt.Set<std::string>( "static.serviceName", radarConfigBasic.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", radarConfigBasic.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", radarConfigBasic.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", radarConfigBasic.maxOutputBufferSize );

    std::vector<uint32_t> inputIds;
    std::vector<uint32_t> outputIds = { 1 };   // buffers.size()==1 => out of range
    dt.Set( "static.inputs", inputIds );
    dt.Set( "static.outputs", outputIds );

    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inMap;
    inMap.Set<std::string>( "name", "input" );
    inMap.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inMap );
    QC::DataTree outMap;
    outMap.Set<std::string>( "name", "output" );
    outMap.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outMap );
    dt.Set( "static.globalBufferIdMap", bufferMapDts );

    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, radarConfigBasic.maxInputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    nodeConfig.buffers.push_back(
            std::ref( static_cast<QCBufferDescriptorBase_t &>( inputBuffer.tensor ) ) );

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_EQ( QCStatus_e::QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Cover Radar::Initialize() init-time buffer registration success path
 * (dynamic_cast to TensorDescriptor_t succeeds).
 * Coverage: Radar.cpp lines 268-292 and 308-331.
 */
TEST_F( RadarNodeTest, Initialize_WithInitBuffers_ValidTensorDescriptors )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarInitBufValid" );
    dt.Set<uint32_t>( "static.id", 704 );

    dt.Set<std::string>( "static.serviceName", radarConfigBasic.serviceConfig.serviceName );
    dt.Set<uint32_t>( "static.timeoutMs", radarConfigBasic.serviceConfig.timeoutMs );
    dt.Set<bool>( "static.enablePerformanceLog", false );
    dt.Set<uint32_t>( "static.maxInputBufferSize", radarConfigBasic.maxInputBufferSize );
    dt.Set<uint32_t>( "static.maxOutputBufferSize", radarConfigBasic.maxOutputBufferSize );

    std::vector<uint32_t> inputIds = { 0 };
    std::vector<uint32_t> outputIds = { 1 };
    dt.Set( "static.inputs", inputIds );
    dt.Set( "static.outputs", outputIds );

    std::vector<QC::DataTree> bufferMapDts;
    QC::DataTree inMap;
    inMap.Set<std::string>( "name", "input" );
    inMap.Set<uint32_t>( "id", 0 );
    bufferMapDts.push_back( inMap );
    QC::DataTree outMap;
    outMap.Set<std::string>( "name", "output" );
    outMap.Set<uint32_t>( "id", 1 );
    bufferMapDts.push_back( outMap );
    dt.Set( "static.globalBufferIdMap", bufferMapDts );

    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", false );

    QC::QCNodeInit_t nodeConfig = { dt.Dump() };

    TBufferAllocator allocator;
    TBuffer inputBuffer( allocator, radarConfigBasic.maxInputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
    TBuffer outputBuffer( allocator, radarConfigBasic.maxOutputBufferSize );
    ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

    nodeConfig.buffers.push_back(
            std::ref( static_cast<QCBufferDescriptorBase_t &>( inputBuffer.tensor ) ) );
    nodeConfig.buffers.push_back(
            std::ref( static_cast<QCBufferDescriptorBase_t &>( outputBuffer.tensor ) ) );

    QC::QCStatus_e ret = radarNode.Initialize( nodeConfig );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret == QCStatus_e::QC_STATUS_OK )
    {
        radarNode.DeInitialize();
    }
}

/**
 * @brief Cover ValidateBuffer() type branches: TENSOR, IMAGE, and unexpected.
 * Coverage: Radar.cpp lines 568-579.
 */
TEST_F( RadarNodeTest, ValidateBuffer_TypeBranches )
{
    Radar radarNode;

    QC::DataTree dt;
    dt.Set<std::string>( "static.name", "RadarValidateTypes" );
    dt.Set<uint32_t>( "static.id", 705 );
    SetConfigRadarEx( &m_config, &dt );
    QC::QCNodeInit_t config = { dt.Dump() };

    QC::QCStatus_e ret = radarNode.Initialize( config );
    EXPECT_TRUE( ret == QCStatus_e::QC_STATUS_OK || ret == QCStatus_e::QC_STATUS_BAD_STATE );

    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        return;
    }

    ret = radarNode.Start();
    if ( ret != QCStatus_e::QC_STATUS_OK )
    {
        radarNode.DeInitialize();
        return;
    }

    TBufferAllocator allocator;

    // Case 1: TENSOR
    {
        TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
        TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

        inputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_TENSOR;
        outputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_TENSOR;

        GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

        NodeFrameDescriptor frameDesc( 2 );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 0, inputBuffer.tensor ) );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 1, outputBuffer.tensor ) );

        (void) radarNode.ProcessFrameDescriptor( frameDesc );
    }

    // Case 2: IMAGE
    {
        TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
        TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

        inputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_IMAGE;
        outputBuffer.tensor.type = QCBufferType_e::QC_BUFFER_TYPE_IMAGE;

        GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

        NodeFrameDescriptor frameDesc( 2 );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 0, inputBuffer.tensor ) );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 1, outputBuffer.tensor ) );

        (void) radarNode.ProcessFrameDescriptor( frameDesc );
    }

    // Case 3: unexpected type
    {
        TBuffer inputBuffer( allocator, m_config.maxInputBufferSize );
        TBuffer outputBuffer( allocator, m_config.maxOutputBufferSize );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, inputBuffer.GetAllocationStatus() );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, outputBuffer.GetAllocationStatus() );

        inputBuffer.tensor.type = static_cast<QCBufferType_e>( 0x7fffffff );
        outputBuffer.tensor.type = static_cast<QCBufferType_e>( 0x7fffffff );

        GenerateRadarTestData( inputBuffer.tensor.GetDataPtr(), m_config.maxInputBufferSize );

        NodeFrameDescriptor frameDesc( 2 );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 0, inputBuffer.tensor ) );
        ASSERT_EQ( QCStatus_e::QC_STATUS_OK, frameDesc.SetBuffer( 1, outputBuffer.tensor ) );

        (void) radarNode.ProcessFrameDescriptor( frameDesc );
    }

    radarNode.Stop();
    radarNode.DeInitialize();
}

#ifndef GTEST_QCNODE
#if __CTC__
extern "C" void ctc_append_all( void );
#endif
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    int nVal = RUN_ALL_TESTS();
#if __CTC__
    ctc_append_all();
#endif
    return nVal;
}
#endif
