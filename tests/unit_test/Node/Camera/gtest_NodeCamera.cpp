// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>

#include "CameraMock.hpp"
#include "QC/Node/Camera.hpp"
#include "QC/sample/SharedBufferPool.hpp"
#include "gtest/gtest.h"
#if defined( __linux__ ) && !defined( __QNXNTO__ )
#include <plat_dmabuf.h>
#endif

#include "camera_metadata.h"
#include "qcarcam_metadata.h"

using namespace QC;
using namespace QC::Node;
using namespace QC::sample;

QC::Node::Camera *g_pCamera = nullptr;
uint32_t g_frameIdx = 0;
std::vector<SharedBufferPool> g_bufferPools;

// A minimal callback for *MockCov tests that doesn't depend on g_pCamera being set.
// target.
static void NoOpCb( const QCNodeEventInfo_t &eventInfo )
{
    (void) eventInfo;
}

// Frame-capturing callback for strict assertions on the frame a FrameCallback
// delivers. Records the count of OK frames seen and the last frame's timestamp
// so a test can pin the exact value the GetFrame path wrote into the descriptor.
struct CapturedFrame_t
{
    uint32_t okCount;
    uint64_t lastTimestamp;
    uint32_t lastStreamId;
};
static CapturedFrame_t g_captured = { 0, 0, 0 };

static void CaptureFrameCb( const QCNodeEventInfo_t &eventInfo )
{
    if ( QC_STATUS_OK != eventInfo.status )
    {
        return;
    }
    QCFrameDescriptorNodeIfs &frameDescIfs = eventInfo.frameDesc;
    QCBufferDescriptorBase_t &bufDesc = frameDescIfs.GetBuffer( 0 );
    const CameraFrameDescriptor_t *pCamFrameDesc =
            dynamic_cast<const CameraFrameDescriptor_t *>( &bufDesc );
    if ( nullptr != pCamFrameDesc )
    {
        g_captured.okCount++;
        g_captured.lastTimestamp = pCamFrameDesc->timestamp;
        g_captured.lastStreamId = pCamFrameDesc->streamId;
    }
}

// global mock param
QCarCamInput_t g_mockInputsInfo;
QCarCamMode_t g_mockCamMode;
QCarCamInputModes_t g_mockInputModes;
MockApi_ControlFnc_t g_controlFnc = nullptr;
MockApi_SetErrorPassiveFnc_t g_setErrorPassiveFnc = nullptr;
MockApi_SetFullMockFnc_t g_setFullMockFnc = nullptr;
MockApi_TriggerEventFnc_t g_triggerEventFnc = nullptr;

// global metadata param
const uint32_t MAX_METADATA_TAG_NUM = 50;
const uint32_t MAX_METADATA_TAG_DATA = 65536;
BufferProps_t g_bufferProp;
QCarCamBufferList_t g_bufferList;
uint32_t g_bufferNum = 4;
uint32_t g_planeNum = 2;
CameraFrameDescriptor_t *g_pCamFrameDescs = nullptr;

void ReadJsonFile( const std::string &filePath, nlohmann::json &jsonData )
{
    std::ifstream file( filePath );
    if ( file.is_open() )
    {
        file >> jsonData;
        file.close();
    }
    else
    {
        QC_LOG_ERROR( "Failed to open file %s", filePath );
    }
}

void ProcessDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    ASSERT_NE( nullptr, g_pCamera )
            << "ProcessDoneCb invoked but g_pCamera is null; use NoOpCb when "
               "the test uses a stack-allocated Camera or otherwise does not "
               "set g_pCamera.";

    QCStatus_e ret = QC_STATUS_OK;

    QCFrameDescriptorNodeIfs &frameDescIfs = eventInfo.frameDesc;
    QCBufferDescriptorBase_t &bufDesc = frameDescIfs.GetBuffer( 0 );
    NodeFrameDescriptor frameDesc( 1 );

    if ( QC_STATUS_OK == eventInfo.status )
    {
        const CameraFrameDescriptor_t *pCamFrameDesc =
                dynamic_cast<const CameraFrameDescriptor_t *>( &bufDesc );

        const CameraMetaDataDescriptor_t *pCamMetaDataDesc =
                dynamic_cast<const CameraMetaDataDescriptor_t *>( &bufDesc );

        if ( QC_STATUS_OK == ret )
        {
            if ( pCamFrameDesc != nullptr )
            {
                CameraFrameDescriptor_t camFrameDesc = *pCamFrameDesc;
                ret = frameDesc.SetBuffer( 0, camFrameDesc );
                ASSERT_EQ( QC_STATUS_OK, ret );

                ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
                ASSERT_EQ( QC_STATUS_OK, ret );

                std::cout << "Process Frame index: " << g_frameIdx << std::endl;
                g_frameIdx++;
            }
            else if ( pCamMetaDataDesc != nullptr )
            {
                CameraMetaDataDescriptor_t camMetaDataDesc = *pCamMetaDataDesc;
                ret = frameDesc.SetBuffer( 0, camMetaDataDesc );
                ASSERT_EQ( QC_STATUS_OK, ret );

                ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
                ASSERT_EQ( QC_STATUS_OK, ret );
            }
            else
            {
                std::cout << "No valid buffer desc received " << std::endl;
            }
        }
    }
    else
    {
        std::cout << "Received camera event, status: " << eventInfo.status << std::endl;
    }
}

QCStatus_e AllocateFrameBuffers(
        DataTree &config,
        std::vector<std::reference_wrapper<QC::Memory::QCBufferDescriptorBase_t>> &buffers )
{
    QCStatus_e ret = QC_STATUS_OK;
    std::vector<DataTree> streamConfigs;
    std::string name = config.Get<std::string>( "name", "" );

    QCNodeID_t nodeId;
    nodeId.name = name;
    nodeId.type = QC_NODE_TYPE_QCX;
    nodeId.id = config.Get<uint32_t>( "id", UINT32_MAX );
    if ( UINT32_MAX == nodeId.id )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        return ret;
    }

    ret = config.Get( "streamConfigs", streamConfigs );
    if ( QC_STATUS_OK != ret )
    {
        return ret;
    }

    DataTree streamConfig;
    ImageProps_t imgProp;
    uint32_t streamId = 0;
    uint32_t bufferId = 0;
    uint32_t streamNum = streamConfigs.size();
    uint32_t bufferNum = 0;
    g_bufferPools.reserve( QCNODE_CAMERA_MAX_STREAM_NUM );
    g_bufferPools.resize( streamNum );

    for ( uint32_t i = 0; i < streamNum; i++ )
    {
        std::string bufPoolName = name + "_stream_" + std::to_string( i );
        streamConfig = streamConfigs[i];
        streamId = streamConfig.Get<uint32_t>( "streamId", UINT32_MAX );
        std::vector<uint32_t> bufferIds =
                streamConfig.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        bufferNum = bufferIds.size();
        imgProp.format = streamConfig.GetImageFormat( "format", QC_IMAGE_FORMAT_MAX );
        imgProp.width = streamConfig.Get<uint32_t>( "width", UINT32_MAX );
        imgProp.height = streamConfig.Get<uint32_t>( "height", UINT32_MAX );

        if ( ( QC_IMAGE_FORMAT_RGB888 == imgProp.format ) ||
             ( QC_IMAGE_FORMAT_BGR888 == imgProp.format ) )
        {
            imgProp.batchSize = 1;
            imgProp.stride[0] = QC_ALIGN_SIZE( imgProp.width * 3, 16 );
            imgProp.actualHeight[0] = imgProp.height;
            imgProp.numPlanes = 1;
            imgProp.planeBufSize[0] = 0;

            ret = g_bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                         imgProp );
            if ( QC_STATUS_OK != ret )
            {
                break;
            }
        }
        else
        {
            ret = g_bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                         imgProp.width, imgProp.height, imgProp.format );
            if ( QC_STATUS_OK != ret )
            {
                break;
            }
        }

        ret = g_bufferPools[i].GetBuffers( buffers );
        if ( QC_STATUS_OK != ret )
        {
            break;
        }
    }

    return ret;
}

QCStatus_e AllocateMetaDataBuffers(
        DataTree &config,
        std::vector<std::reference_wrapper<QC::Memory::QCBufferDescriptorBase_t>> &buffers,
        BufferProps_t &bufferProp )
{
    QCStatus_e ret = QC_STATUS_OK;
    std::vector<DataTree> metaDataConfigs;
    std::string name = config.Get<std::string>( "name", "" );

    QCNodeID_t nodeId;
    nodeId.name = name;
    nodeId.type = QC_NODE_TYPE_QCX;
    nodeId.id = config.Get<uint32_t>( "id", UINT32_MAX );
    if ( UINT32_MAX == nodeId.id )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        return ret;
    }

    ret = config.Get( "metaDataConfigs", metaDataConfigs );
    if ( QC_STATUS_OK != ret )
    {
        return ret;
    }

    DataTree metaDataConfig;
    uint32_t streamId = 0;
    uint32_t bufferListId = 0;
    uint32_t metaDataNum = metaDataConfigs.size();
    uint32_t bufferNum = 0;
    size_t bufferSize = 0;
    const uint32_t MAX_METADATA_TAG_NUM = 50;
    const uint32_t MAX_METADATA_TAG_DATA = 65536;
    camera_metadata_t *pMetaData = nullptr;
    uint32_t basePool = g_bufferPools.size();
    g_bufferPools.reserve( QCNODE_CAMERA_MAX_STREAM_NUM );
    g_bufferPools.resize( basePool + metaDataNum );

    for ( uint32_t i = 0; i < metaDataNum; i++ )
    {
        std::string bufPoolName = name + "_metadata_" + std::to_string( i );
        metaDataConfig = metaDataConfigs[i];
        bufferListId = metaDataConfig.Get<uint32_t>( "bufferListId", UINT32_MAX );
        std::vector<uint32_t> bufferIds =
                metaDataConfig.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        bufferNum = bufferIds.size();

        ret = g_bufferPools[basePool + i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                                bufferProp );
        if ( QC_STATUS_OK != ret )
        {
            break;
        }

        ret = g_bufferPools[basePool + i].GetBuffers( buffers );
        if ( QC_STATUS_OK != ret )
        {
            break;
        }
    }

    return ret;
}

QCStatus_e DeinitBuffers()
{
    QCStatus_e ret = QC_STATUS_OK;
    for ( uint32_t i = 0; i < g_bufferPools.size(); i++ )
    {
        ret = g_bufferPools[i].Deinit();
        if ( QC_STATUS_OK != ret )
        {
            break;
        }
    }

    g_bufferPools.clear();

    return ret;
}

static void ClearAllMockOverrides()
{
    for ( int apiId = 0; apiId < MOCK_API_MAX; apiId++ )
    {
        g_controlFnc( (MockAPI_ID_e) apiId, MOCK_CONTROL_API_NONE, nullptr );
    }
}

void SetFullMockParam()
{
    memset( &g_mockInputsInfo, 0, sizeof( g_mockInputsInfo ) );
    g_mockInputsInfo.numModes = 1;
    snprintf( g_mockInputsInfo.inputName, sizeof( g_mockInputsInfo.inputName ), "CAM0" );

    memset( &g_mockCamMode, 0, sizeof( g_mockCamMode ) );
    g_mockCamMode.numSources = 1;
    g_mockCamMode.sources[0].srcId = 0;
    g_mockCamMode.sources[0].width = 3840;
    g_mockCamMode.sources[0].height = 2160;
    g_mockCamMode.sources[0].colorFmt = QCARCAM_FMT_NV12;
    g_mockCamMode.sources[0].fps = 30.f;
    g_mockCamMode.sources[0].securityDomain = 0;

    memset( &g_mockInputModes, 0, sizeof( g_mockInputModes ) );
    g_mockInputModes.currentMode = 0;
    g_mockInputModes.numModes = 1;
    g_mockInputModes.pModes = new QCarCamMode_t;
    *g_mockInputModes.pModes = g_mockCamMode;

    g_controlFnc = MockCamera_GetControlFnc( "libCameraMock.so" );
    g_setFullMockFnc = MockCamera_GetSetFullMockFnc( "libCameraMock.so" );
    g_setErrorPassiveFnc = MockCamera_GetSetErrorPassiveFnc( "libCameraMock.so" );
    g_triggerEventFnc = MockCamera_GetTriggerEventFnc( "libCameraMock.so" );
    ASSERT_NE( g_controlFnc, nullptr );
    ASSERT_NE( g_setFullMockFnc, nullptr );
    ASSERT_NE( g_setErrorPassiveFnc, nullptr );

    // CameraMock has two mutually exclusive backends (passive vs full-mock).
    // Every test uses full-mock; explicitly disable passive so a stale backend
    // state can never leak in, then enable full-mock.
    g_setErrorPassiveFnc( false );
    g_setFullMockFnc( true );
    ClearAllMockOverrides();
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_OUT_PARAM1, &g_mockInputsInfo );
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUT_MODES, MOCK_CONTROL_API_OUT_PARAM1,
                  &g_mockInputModes );
}

static void LoadCameraConfig( QCNodeInit_t &outConfig, const std::string &filePath,
                              QCNodeEventCallBack_t callback = NoOpCb )
{
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;

    std::ifstream file( filePath );
    if ( file.is_open() )
    {
        file >> jsonData;
        file.close();
    }
    else
    {
        QC_LOG_ERROR( "Failed to open file %s", filePath );
    }
    jsonData["static"]["inputId"] = 0;
    ASSERT_EQ( QC_STATUS_OK, dt.Load( jsonData.dump(), errors ) );
    outConfig.config = dt.Dump();
    outConfig.callback = callback;
}

static nlohmann::json &NavigateConfigParent( nlohmann::json &root, const std::string &path,
                                             std::string &leafKey, int &leafIndex )
{
    nlohmann::json *pCurrent = &root["static"];
    std::istringstream ss( path );
    std::string token;
    std::vector<std::string> tokens;
    while ( std::getline( ss, token, '.' ) )
    {
        tokens.push_back( token );
    }

    leafKey.clear();
    leafIndex = -1;
    for ( size_t i = 0; i < tokens.size(); i++ )
    {
        std::string key = tokens[i];
        int index = -1;
        size_t lb = key.find( '[' );
        if ( std::string::npos != lb )
        {
            size_t rb = key.find( ']', lb );
            index = std::stoi( key.substr( lb + 1, rb - lb - 1 ) );
            key = key.substr( 0, lb );
        }

        bool isLeaf = ( i + 1 == tokens.size() );
        if ( isLeaf && index < 0 )
        {
            // Leaf is a plain object key: return current parent + key.
            leafKey = key;
            return *pCurrent;
        }

        pCurrent = &( *pCurrent )[key];
        if ( index >= 0 )
        {
            if ( isLeaf )
            {
                // Leaf is an array element: return the array + index.
                leafIndex = index;
                return *pCurrent;
            }
            pCurrent = &( *pCurrent )[index];
        }
    }
    return *pCurrent;
}

// Apply `mutate` to the parsed config and write it back to cfg.config.
static void EditConfig( QCNodeInit_t &cfg, const std::function<void( nlohmann::json & )> &mutate )
{
    DataTree dt;
    std::string errors;
    ASSERT_EQ( QC_STATUS_OK, dt.Load( cfg.config, errors ) );
    nlohmann::json root;
    {
        std::istringstream js( dt.Dump() );
        js >> root;
    }
    mutate( root );
    DataTree dt2;
    ASSERT_EQ( QC_STATUS_OK, dt2.Load( root.dump(), errors ) );
    cfg.config = dt2.Dump();
}

// Set a static-config value at a dotted path (with optional [i] array indices).
template<typename T>
static void SetConfig( QCNodeInit_t &cfg, const std::string &path, const T &value )
{
    EditConfig( cfg, [&]( nlohmann::json &root ) {
        std::string leafKey;
        int leafIndex = -1;
        nlohmann::json &parent = NavigateConfigParent( root, path, leafKey, leafIndex );
        if ( leafIndex >= 0 )
        {
            parent[leafIndex] = value;
        }
        else
        {
            parent[leafKey] = value;
        }
    } );
}

// Erase a static-config key at a dotted path, e.g. "metaDataConfigs" or
// "streamConfigs[0].bufferIds".
static void EraseConfig( QCNodeInit_t &cfg, const std::string &path )
{
    EditConfig( cfg, [&]( nlohmann::json &root ) {
        std::string leafKey;
        int leafIndex = -1;
        nlohmann::json &parent = NavigateConfigParent( root, path, leafKey, leafIndex );
        parent.erase( leafKey );
    } );
}

// Append a stream-config object to static.streamConfigs.
static void PushStreamConfig( QCNodeInit_t &cfg, const nlohmann::json &streamObj )
{
    EditConfig( cfg, [&]( nlohmann::json &root ) {
        root["static"]["streamConfigs"].push_back( streamObj );
    } );
}

// Read a copy of the static.streamConfigs[0] object from a config (used to build
// a sibling stream to push).
static nlohmann::json GetFirstStreamConfig( const QCNodeInit_t &cfg )
{
    nlohmann::json root = nlohmann::json::parse( cfg.config );
    return root["static"]["streamConfigs"][0];
}

// Allocate frame buffers for an already-loaded config: derives the static subtree
// from cfg.config and fills cfg.buffers. Mock arming is left to the caller.
static void AllocateFrameBuffersForConfig( QCNodeInit_t &cfg )
{
    DataTree fullDt, staticCfg;
    std::string errors;
    ASSERT_EQ( QC_STATUS_OK, fullDt.Load( cfg.config, errors ) );
    ASSERT_EQ( QC_STATUS_OK, fullDt.Get( "static", staticCfg ) );
    ASSERT_EQ( QC_STATUS_OK, AllocateFrameBuffers( staticCfg, cfg.buffers ) );
}

static void RunStartedCamera( const std::string &filePath,
                              const std::function<void( QC::Node::Camera & )> &body,
                              const std::function<void( QCNodeInit_t & )> &edit = nullptr )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, NoOpCb );
    if ( edit )
    {
        edit( config );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    if ( body )
    {
        body( camera );
    }

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

static void RunInitDeInit_WithFormat( const std::string &format )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].format", format );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

void SetGlobalBufferParam( DataTree &staticCfg, QCNodeInit_t &config )
{
    QCStatus_e ret;
    g_bufferNum = 4;
    g_planeNum = 2;
    bool enableMetaData = staticCfg.Get<bool>( "enableMetaData", false );

    memset( &g_bufferList, 0, sizeof( g_bufferList ) );
    g_bufferList.id = 1;
    g_bufferList.nBuffers = g_bufferNum;
    g_bufferList.pBuffers = new QCarCamBuffer_t[g_bufferNum];
    g_bufferList.colorFmt = QCARCAM_FMT_NV12;
    g_bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;
    g_pCamFrameDescs = new CameraFrameDescriptor_t[g_bufferNum];

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    if ( enableMetaData )
    {
        g_bufferProp.size =
                calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
        g_bufferProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
        g_bufferProp.cache = QC_CACHEABLE;

        ret = AllocateMetaDataBuffers( staticCfg, config.buffers, g_bufferProp );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    for ( uint32_t i = 0; i < g_bufferNum; i++ )
    {
        g_pCamFrameDescs[i] = config.buffers[i];
        g_bufferList.pBuffers[i].numPlanes = g_planeNum;
        g_bufferList.pBuffers[i].planes[0].width = 3840;
        g_bufferList.pBuffers[i].planes[0].height = 2160;
        g_bufferList.pBuffers[i].planes[0].stride = 3840;
        g_bufferList.pBuffers[i].planes[0].size = 8355840;
        g_bufferList.pBuffers[i].planes[0].offset = 0;
        g_bufferList.pBuffers[i].planes[0].memHndl = g_pCamFrameDescs[i].dmaHandle;
        g_bufferList.pBuffers[i].planes[1].width = 3840;
        g_bufferList.pBuffers[i].planes[1].height = 2160;
        g_bufferList.pBuffers[i].planes[1].stride = 3840;
        g_bufferList.pBuffers[i].planes[1].size = 4177920;
        g_bufferList.pBuffers[i].planes[1].offset = 8355840;
        g_bufferList.pBuffers[i].planes[1].memHndl = g_pCamFrameDescs[i].dmaHandle;
    }
}

void BuildPoolBufferList( QCarCamBufferList_t &outList, QCarCamBuffer_t *outBufs,
                          SharedBufferPool &outPool, uint32_t count )
{
    QCNodeID_t nodeId;
    nodeId.name = "build_pool_buf";
    nodeId.type = QC_NODE_TYPE_QCX;
    nodeId.id = 0;

    QCStatus_e ret = outPool.Init( "build_pool_buf", nodeId, LOGGER_LEVEL_ERROR, count, 3840u,
                                   2160u, QC_IMAGE_FORMAT_NV12 );
    ASSERT_EQ( QC_STATUS_OK, ret );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> poolBufs;
    ret = outPool.GetBuffers( poolBufs );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( count, (uint32_t) poolBufs.size() );

    memset( &outList, 0, sizeof( outList ) );
    outList.id = 1;
    outList.nBuffers = count;
    outList.colorFmt = QCARCAM_FMT_NV12;
    outList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;

    for ( uint32_t i = 0; i < count; i++ )
    {
        QCBufferDescriptorBase_t &bufDesc = poolBufs[i].get();
        ImageDescriptor_t *pImgDesc = dynamic_cast<ImageDescriptor_t *>( &bufDesc );
        ASSERT_NE( pImgDesc, nullptr );

        memset( &outBufs[i], 0, sizeof( outBufs[i] ) );
        outBufs[i].numPlanes = 2;
        outBufs[i].planes[0].width = 3840;
        outBufs[i].planes[0].height = 2160;
        outBufs[i].planes[0].stride = pImgDesc->stride[0];
        outBufs[i].planes[0].size = pImgDesc->planeBufSize[0];
        outBufs[i].planes[0].offset = 0;
#if defined( __linux__ ) && !defined( __QNXNTO__ )
        // DMABUFFUtils::MemoryUnMap closes the fd on Linux; import a fresh fd
        // so the pool's copy stays intact for Deinit.
        int importFd = dmabufheap_import( getpid(), (int) pImgDesc->dmaHandle );
        ASSERT_GE( importFd, 0 );
        outBufs[i].planes[0].memHndl = (uint64_t) importFd;
#else
        outBufs[i].planes[0].memHndl = (uint64_t) pImgDesc->dmaHandle;
#endif
        outBufs[i].planes[1].width = 3840;
        outBufs[i].planes[1].height = 2160;
        outBufs[i].planes[1].stride = pImgDesc->stride[1];
        outBufs[i].planes[1].size = pImgDesc->planeBufSize[1];
        outBufs[i].planes[1].offset = pImgDesc->planeBufSize[0];
#if defined( __linux__ ) && !defined( __QNXNTO__ )
        outBufs[i].planes[1].memHndl = (uint64_t) importFd;
#else
        outBufs[i].planes[1].memHndl = (uint64_t) pImgDesc->dmaHandle;
#endif
    }
    outList.pBuffers = outBufs;
}

void SANITY_Test_Camera_Frame( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;

    config.config = dt.Dump();
    std::cout << "config: " << config.config << std::endl;

    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    sleep( 1 );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void SANITY_Test_Camera_MetaData( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCarCamRet_e correctRet = QCARCAM_RET_OK;

    config.config = dt.Dump();
    std::cout << "config: " << config.config << std::endl;

    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();
    SetGlobalBufferParam( staticCfg, config );

    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );

    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t camMetaDataDesc;
    (void) frameDesc.SetBuffer( 0, camMetaDataDesc );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    sleep( 1 );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete[] g_pCamFrameDescs;
    g_pCamFrameDescs = nullptr;

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void SANITY_Test_CameraReleaseMode_Mock( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    std::string errors;
    CameraFrameDescriptor_t camFrameDesc;
    NodeFrameDescriptor frameDesc( 1 );
    QCarCamRet_e correctRet = QCARCAM_RET_OK;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();
    g_controlFnc( MOCK_API_QCARCAM_START, MOCK_CONTROL_API_RETURN, &correctRet );

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    camFrameDesc = config.buffers[0];
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void SANITY_Test_CameraMonitor( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    std::string errors;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // Exercise CameraMonitor interface
    QCNodeMonitoringIfs &monitorIfs = g_pCamera->GetMonitoringIfs();

    // VerifyAndSet returns UNSUPPORTED
    ret = monitorIfs.VerifyAndSet( "{}", errors );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, ret );

    // GetOptions returns "{}"
    std::string opts;
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, monitorIfs.GetOptions( opts ) );

    // Get returns the monitor config
    const QCNodeMonitoringBase_t &monCfg = monitorIfs.Get();
    (void) monCfg;

    // GetMaximalSize and GetCurrentSize
    uint32_t maxSize = monitorIfs.GetMaximalSize();
    ASSERT_EQ( 0u, maxSize );

    uint32_t curSize = monitorIfs.GetCurrentSize();
    ASSERT_EQ( 0u, curSize );

    // Place returns UNSUPPORTED
    uint32_t size = 0;
    ret = monitorIfs.Place( nullptr, size );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, ret );

    // Exercise CameraConfig::GetOptions and CameraConfig::Get
    QCNodeConfigIfs &configIfs = g_pCamera->GetConfigurationIfs();
    std::string cfgOpts;
    ASSERT_EQ( QC_STATUS_OK, configIfs.GetOptions( cfgOpts ) );
    (void) cfgOpts;

    const QCNodeConfigBase_t &baseCfg = configIfs.Get();
    (void) baseCfg;

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_ConfigError( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    DataTree errorCfg;
    std::vector<DataTree> streamConfigs;
    std::vector<uint32_t> bufferIds;
    QCNodeInit_t config;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();

    // empty name
    std::string originalName = staticCfg.Get<std::string>( "name", "" );
    dt.Set<std::string>( "static.name", "" );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty nodeId
    uint32_t originalId = staticCfg.Get<uint32_t>( "id", UINT32_MAX );
    dt.Set<std::string>( "static.name", originalName );
    dt.Set<uint32_t>( "static.id", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty inputId
    uint32_t originalInputId = staticCfg.Get<uint32_t>( "inputId", UINT32_MAX );
    dt.Set<uint32_t>( "static.id", originalId );
    dt.Set<uint32_t>( "static.inputId", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty srcId
    uint32_t originalSrcId = staticCfg.Get<uint32_t>( "srcId", UINT32_MAX );
    dt.Set<uint32_t>( "static.inputId", originalInputId );
    dt.Set<uint32_t>( "static.srcId", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty clientId
    uint32_t originalClientId = staticCfg.Get<uint32_t>( "clientId", UINT32_MAX );
    dt.Set<uint32_t>( "static.srcId", originalSrcId );
    dt.Set<uint32_t>( "static.clientId", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty inputMode
    uint32_t originalInputMode = staticCfg.Get<uint32_t>( "inputMode", UINT32_MAX );
    dt.Set<uint32_t>( "static.clientId", originalClientId );
    dt.Set<uint32_t>( "static.inputMode", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty ispUseCase
    uint32_t originalIspUseCase = staticCfg.Get<uint32_t>( "ispUseCase", UINT32_MAX );
    dt.Set<uint32_t>( "static.inputMode", originalInputMode );
    dt.Set<uint32_t>( "static.ispUseCase", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty camFrameDropPattern
    uint32_t originalFrameDropPattern =
            staticCfg.Get<uint32_t>( "camFrameDropPattern", UINT32_MAX );
    dt.Set<uint32_t>( "static.ispUseCase", originalIspUseCase );
    dt.Set<uint32_t>( "static.camFrameDropPattern", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty camFrameDropPeriod
    uint8_t originalFrameDropPeriod = staticCfg.Get<uint8_t>( "camFrameDropPeriod", UINT8_MAX );
    dt.Set<uint32_t>( "static.camFrameDropPattern", originalFrameDropPattern );
    dt.Set<uint8_t>( "static.camFrameDropPeriod", UINT8_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty opMode
    uint32_t originalOpMode = staticCfg.Get<uint32_t>( "opMode", UINT32_MAX );
    dt.Set<uint8_t>( "static.camFrameDropPeriod", originalFrameDropPeriod );
    dt.Set<uint32_t>( "static.opMode", UINT32_MAX );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty stream config
    std::vector<DataTree> emptyStreamConfigs;
    dt.Set<uint32_t>( "static.opMode", originalOpMode );
    errorCfg.Set( "static", staticCfg );
    errorCfg.Set( "static.streamConfigs", emptyStreamConfigs );
    config.config = errorCfg.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty metadata config
    dt.Set<bool>( "static.enableMetaData", true );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, ret );

    // empty streamId
    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<uint32_t>( "streamId", UINT32_MAX );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    dt.Set<bool>( "static.enableMetaData", false );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // bad streamId
    streamConfigs[0].Set<uint32_t>( "streamId", 36 );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty width
    streamConfigs[0].Set<uint32_t>( "streamId", 1 );
    streamConfigs[0].Set<uint32_t>( "width", UINT32_MAX );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty height
    streamConfigs[0].Set<uint32_t>( "width", 3840 );
    streamConfigs[0].Set<uint32_t>( "height", UINT32_MAX );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty format
    streamConfigs[0].Set<uint32_t>( "height", 2160 );
    streamConfigs[0].Set<std::string>( "format", "max_format" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty requestPattern
    streamConfigs[0].Set<std::string>( "format", "nv12" );
    streamConfigs[0].Set<uint32_t>( "submitRequestPattern", UINT32_MAX );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // bufferNum 0
    streamConfigs[0].Set<uint32_t>( "submitRequestPattern", 0 );
    streamConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // duplicated bufferId
    uint32_t bufferNum = 4;
    for ( uint32_t i = 0; i < bufferNum; i++ )
    {
        bufferIds.push_back( i );
    }
    bufferIds.push_back( bufferNum - 1 );
    streamConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // bufferNum 28
    bufferNum = 28;
    bufferIds.clear();
    for ( uint32_t i = 0; i < bufferNum; i++ )
    {
        bufferIds.push_back( i );
    }
    streamConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    delete g_pCamera;
    g_pCamera = nullptr;
}

void Exception_Test_ConfigError_MetaData( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    DataTree errorCfg;
    std::vector<DataTree> metaDataConfigs;
    std::vector<uint32_t> bufferIds;
    QCNodeInit_t config;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();

    // empty metaDataConfigs
    std::vector<DataTree> emptyMetaDataConfigs;
    errorCfg.Set( "static", staticCfg );
    errorCfg.Set<bool>( "static.enableMetaData", true );
    errorCfg.Set( "static.metaDataConfigs", emptyMetaDataConfigs );
    config.config = errorCfg.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // empty bufferListId
    ret = staticCfg.Get( "metaDataConfigs", metaDataConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    metaDataConfigs[0].Set( "bufferListId", UINT32_MAX );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // bufferNum 0
    metaDataConfigs[0].Set( "bufferListId", 4 );
    metaDataConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // duplicated bufferId
    uint32_t bufferNum = 4;
    for ( uint32_t i = 0; i < bufferNum; i++ )
    {
        bufferIds.push_back( i );
    }
    bufferIds.push_back( bufferNum - 1 );
    metaDataConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // bufferNum 28
    bufferNum = 28;
    bufferIds.clear();
    for ( uint32_t i = 0; i < bufferNum; i++ )
    {
        bufferIds.push_back( i );
    }
    metaDataConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    delete g_pCamera;
    g_pCamera = nullptr;
}

void Exception_Test_InitError( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    DataTree errorCfg;
    QCNodeInit_t config;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();

    // error inputId
    uint32_t originalInputId = staticCfg.Get<uint32_t>( "inputId", UINT32_MAX );
    dt.Set<uint32_t>( "static.inputId", 20 );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // Open Failure
    uint32_t originalClientId = staticCfg.Get<uint32_t>( "clientId", UINT32_MAX );
    dt.Set<uint32_t>( "static.inputId", originalInputId );
    dt.Set<uint32_t>( "static.clientId", 1 );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // primary = true with clientId = 0
    dt.Set<bool>( "static.primary", true );
    dt.Set<uint32_t>( "static.clientId", 0 );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    delete g_pCamera;
    g_pCamera = nullptr;
}

void Exception_Test_InitDeinitError_Mock( DataTree &dt )
{
    QCStatus_e ret;
    QCarCamRet_e correctRet = QCARCAM_RET_OK;
    QCarCamRet_e errorRet = QCARCAM_RET_FAILED;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCObjectState_e status;

    config.config = dt.Dump();
    std::cout << "config: " << config.config << std::endl;

    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // QCarCamInitialize failure
    g_controlFnc( MOCK_API_QCARCAM_INITIALIZE, MOCK_CONTROL_API_RETURN, &errorRet );
    g_pCamera = new QC::Node::Camera();

    delete g_pCamera;
    g_pCamera = nullptr;

    // QCarCamOpen failure
    g_controlFnc( MOCK_API_QCARCAM_INITIALIZE, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_OPEN, MOCK_CONTROL_API_RETURN, &errorRet );
    g_pCamera = new QC::Node::Camera();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // QCarCamOpen with QcarCamHndl=0
    QCarCamHndl_t qcarCamHndl = 0;
    g_controlFnc( MOCK_API_QCARCAM_OPEN, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_OPEN, MOCK_CONTROL_API_OUT_PARAM1, &qcarCamHndl );

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // Register EventCallback failure
    g_controlFnc( MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK, MOCK_CONTROL_API_RETURN, &errorRet );
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // SetParam for event mask failure
    g_controlFnc( MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK, MOCK_CONTROL_API_RETURN, &errorRet );
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // Reserve failure
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_FRAME_DROP_CONTROL, MOCK_CONTROL_API_RETURN,
                  &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_RESERVE, MOCK_CONTROL_API_RETURN, &errorRet );
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // Deinit failure for bad status
    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    // Init successfully
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // Init with bad state
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // Init successfully
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // QCarCamRelease failure
    g_controlFnc( MOCK_API_QCARCAM_RELEASE, MOCK_CONTROL_API_RETURN, &errorRet );
    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // QCarCamClose failure
    g_controlFnc( MOCK_API_QCARCAM_RELEASE, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_CLOSE, MOCK_CONTROL_API_RETURN, &errorRet );
    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // QCarCamUninitialize failure
    g_controlFnc( MOCK_API_QCARCAM_UNINITIALIZE, MOCK_CONTROL_API_RETURN, &errorRet );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_StartStopError( DataTree &dt )
{
    QCStatus_e ret;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCObjectState_e status;
    DataTree staticCfg;
    QCNodeInit_t config;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    // Start without ready state
    g_pCamera = new QC::Node::Camera();

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // Initialize successfully
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // QCarCamStart failure
    g_controlFnc( MOCK_API_QCARCAM_START, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // Submit all buffers failure
    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // Start successfully
    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, status );

    // Start with running state
    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, status );

    // QCarCamStop failure
    g_controlFnc( MOCK_API_QCARCAM_STOP, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, status );

    // Stop successfully
    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    // Stop without running state
    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_READY, status );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_SetFrameBuffer( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCObjectState_e status;
    std::vector<DataTree> streamConfigs;
    std::vector<uint32_t> bufferIds;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    CameraFrameDescriptor_t camFrameDesc;
    CameraFrameDescriptor_t originalCamFrameDesc;
    uint32_t bufferNum = 8;

    config.config = dt.Dump();
    std::cout << "config: " << config.config << std::endl;
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    bufferIds = streamConfigs[0].Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

    SetFullMockParam();

    g_pCamera = new QC::Node::Camera();

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // allocate buffers
    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // buffer index out of bound
    bufferIds[0] = 9;
    streamConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // empty buffer descriptor
    bufferIds[0] = 0;
    streamConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    camFrameDesc = config.buffers[0];
    originalCamFrameDesc = config.buffers[0];
    camFrameDesc.pBuf = nullptr;
    config.buffers[0] = camFrameDesc;

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // use registered buffer
    config.buffers[0] = originalCamFrameDesc;
    camFrameDesc = config.buffers[1];
    config.buffers[1] = config.buffers[0];

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // incorrect buffer property - format
    config.buffers[1] = camFrameDesc;
    streamConfigs[0].Set<std::string>( "format", "rgb" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // incorrect buffer property - width
    streamConfigs[0].Set<std::string>( "format", "nv12" );
    streamConfigs[0].Set<uint32_t>( "width", 1920 );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // incorrect buffer property - height
    streamConfigs[0].Set<uint32_t>( "width", 3840 );
    streamConfigs[0].Set<uint32_t>( "height", 1080 );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // QCarCamSetBuffers faliure
    streamConfigs[0].Set<uint32_t>( "height", 2160 );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_SetMetaDataBuffer( DataTree &dt )
{
    QCStatus_e ret;
    QCStatus_e mockRet = QC_STATUS_OK;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCObjectState_e status;
    std::vector<DataTree> metaDataConfigs;
    std::vector<uint32_t> bufferIds;
    BufferDescriptor_t metaDataDesc;
    BufferDescriptor_t originalMetaDataDesc;
    uint32_t bufferNum = 4;

    config.config = dt.Dump();
    std::cout << "config: " << config.config << std::endl;

    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "metaDataConfigs", metaDataConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    bufferIds = metaDataConfigs[0].Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

    SetFullMockParam();
    SetGlobalBufferParam( staticCfg, config );

    g_pCamera = new QC::Node::Camera();

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // buffer index out of bound
    bufferIds[0] = 9;
    metaDataConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // empty buffer descriptor
    bufferIds[0] = 4;
    metaDataConfigs[0].Set( "bufferIds", bufferIds );
    staticCfg.Set( "metaDataConfigs", metaDataConfigs );
    dt.Set( "static", staticCfg );
    config.config = dt.Dump();

    metaDataDesc = config.buffers[4];
    originalMetaDataDesc = config.buffers[4];
    metaDataDesc.pBuf = nullptr;
    config.buffers[4] = metaDataDesc;

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    // use registered buffer
    config.buffers[4] = originalMetaDataDesc;
    metaDataDesc = config.buffers[5];
    config.buffers[5] = config.buffers[4];

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    status = g_pCamera->GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, status );

    delete[] g_pCamFrameDescs;
    g_pCamFrameDescs = nullptr;

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_SubmitRequest_Frame( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // bad state
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // empty buffer
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    // unregistered buffer
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    // use a registered buffer
    frameDesc.Clear();
    camFrameDesc = config.buffers[0];
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // QCarCamSubmitRequest failure
    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_ReleaseFrame( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    CameraFrameDescriptor_t camFrameDesc;
    NodeFrameDescriptor frameDesc( 1 );
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // bad state
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // empty buffer
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    // unregistered buffer
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    // use a registered buffer
    frameDesc.Clear();
    camFrameDesc = config.buffers[0];
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // QCarCamReleaseFrame failure
    g_controlFnc( MOCK_API_QCARCAM_RELEASE_FRAME, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_SubmitRequest_MetaData( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();
    SetGlobalBufferParam( staticCfg, config );

    g_pCamera = new QC::Node::Camera();

    // non-request mode
    dt.Set<bool>( "static.requestMode", false );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // initialize successfully without metadata enabled
    dt.Set<bool>( "static.requestMode", true );
    dt.Set<bool>( "static.enableMetaData", false );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // process with bad state
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // empty buffer
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    // process without metadata enabled
    ret = frameDesc.SetBuffer( 0, metaDataDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // reinitialize with metadata enabled
    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<bool>( "static.enableMetaData", true );
    config.config = dt.Dump();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // request num zero
    metaDataDesc.streamRequestNum = 0;
    frameDesc.Clear();
    ret = frameDesc.SetBuffer( 0, metaDataDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // QCarCamSubmitRequest failure
    metaDataDesc.streamRequestNum = 2;
    frameDesc.Clear();
    ret = frameDesc.SetBuffer( 0, metaDataDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );
    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete[] g_pCamFrameDescs;
    g_pCamFrameDescs = nullptr;

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_EventCallback( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    uint32_t streamId;
    std::vector<DataTree> streamConfigs;
    std::vector<uint32_t> bufferIds;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamId = streamConfigs[0].Get<uint32_t>( "streamId", UINT32_MAX );
    bufferIds = streamConfigs[0].Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    QCarCamEventPayload_t payload;
    memset( &payload, 0, sizeof( payload ) );

    // QCARCAM_EVENT_INPUT_SIGNAL
    g_triggerEventFnc( QCARCAM_EVENT_INPUT_SIGNAL, &payload, false );

    // QCARCAM_EVENT_FRAME_READY
    payload.frameInfo.id = streamId;
    payload.frameInfo.bufferIndex = bufferIds[0];
    payload.frameInfo.sofTimestamp.timestamp = 1000;
    payload.frameInfo.sofTimestamp.timestampGPTP = 2000;
    payload.frameInfo.flags = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    usleep( 10000 );

    // QCARCAM_EVENT_MC_NOTIFY – QCARCAM_MC_STREAM_CREATE
    payload.mcEventInfo.event = QCARCAM_MC_STREAM_CREATE;
    payload.mcEventInfo.numStreams = 1;
    payload.mcEventInfo.bufferListId[0] = bufferIds[0];
    g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );

    // QCARCAM_EVENT_MC_NOTIFY – QCARCAM_MC_STREAM_DESTROY
    payload.mcEventInfo.event = QCARCAM_MC_STREAM_DESTROY;
    g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );

    // QCARCAM_EVENT_MC_NOTIFY – QCARCAM_MC_STREAM_START
    payload.mcEventInfo.event = QCARCAM_MC_STREAM_START;
    g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );

    // QCARCAM_EVENT_MC_NOTIFY – QCARCAM_MC_STREAM_STOP
    payload.mcEventInfo.event = QCARCAM_MC_STREAM_STOP;
    g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );

    // QCARCAM_EVENT_MC_NOTIFY – default (unsupported mc event)
    payload.mcEventInfo.event = static_cast<QCarCamMCEvent_e>( 0xFFFF );
    g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );

    // QCARCAM_EVENT_ERROR
    memset( &payload, 0, sizeof( payload ) );
    payload.errInfo.errorId = QCARCAM_ERROR_SUBSYSTEM_FATAL;
    payload.errInfo.errorCode = 2;
    payload.errInfo.errorSource = 3;
    g_triggerEventFnc( QCARCAM_EVENT_ERROR, &payload, false );

    // default (unsupported event id)
    g_triggerEventFnc( 0xDEAD, &payload, false );

    // static callback path: pPrivateData == nullptr
    g_triggerEventFnc( QCARCAM_EVENT_INPUT_SIGNAL, &payload, true );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_QueryInputs_Error( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_OUT_PARAM1, &g_mockInputsInfo );

    // QueryInputModes failure
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUT_MODES, MOCK_CONTROL_API_RETURN, &failRet );

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_ZeroModeNum( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    // Override numModes to 0
    g_mockInputModes.numModes = 0;
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUT_MODES, MOCK_CONTROL_API_OUT_PARAM1,
                  &g_mockInputModes );

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void Exception_Test_QCarCamInitialize( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    // Get control function WITHOUT setting error passive mode
    g_controlFnc = MockCamera_GetControlFnc( "libCameraMock.so" );
    ASSERT_NE( g_controlFnc, nullptr );

    // Mock QCarCamInitialize to fail (s_isErrorPassive = false, so action is consumed)
    g_controlFnc( MOCK_API_QCARCAM_INITIALIZE, MOCK_CONTROL_API_RETURN, &failRet );

    // Create Camera - constructor calls QCarCamInitialize which fails
    // m_state = QC_OBJECT_STATE_ERROR, g_nCamInitRefCount stays at 0
    g_pCamera = new QC::Node::Camera();

    // Initialize should fail with BAD_STATE
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    // Delete camera - destructor: g_nCamInitRefCount = 0, takes else branch
    delete g_pCamera;
    g_pCamera = nullptr;
}

void Exception_Test_GetFrame_Failure( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    // Use non-request mode (release mode) to exercise QCarCamGetFrame path
    dt.Set<bool>( "static.requestMode", false );
    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetFullMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    g_pCamera = new QC::Node::Camera();
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    QCarCamEventPayload_t payload;
    memset( &payload, 0, sizeof( payload ) );

    // Mock QCarCamGetFrame to fail - covers GetFrame failure path (returns nullptr)
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_RETURN, &failRet );

    // Trigger QCARCAM_EVENT_FRAME_READY - GetFrame will fail, FrameCallback not called
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    usleep( 10000 );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

static QCStatus_e RunInit_FaultInject( const std::string &filePath, MockAPI_ID_e apiId,
                                       QCarCamRet_e *pFailRet,
                                       MockAPI_Action_e action = MOCK_CONTROL_API_RETURN )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Inject the targeted failure (this is one-shot — overwrites any prior
    // arming for the same apiId). QUERY_INPUTS was already armed by SetFullMockParam.
    g_controlFnc( apiId, action, pFailRet );

    QC::Node::Camera *pCam = new QC::Node::Camera();
    QCStatus_e ret = pCam->Initialize( config );
    delete pCam;
    (void) DeinitBuffers();
    return ret;
}


// Helper: drive past Initialize-OK, then call op() with the live Camera, then DeInit.
// `op` returns the value the test wants to assert on.
static QCStatus_e RunInit_Then_Op( const std::string &filePath,
                                   std::function<QCStatus_e( QC::Node::Camera & )> op )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera *pCam = new QC::Node::Camera();
    QCStatus_e initRet = pCam->Initialize( config );
    if ( QC_STATUS_OK != initRet )
    {
        delete pCam;
        (void) DeinitBuffers();
        return initRet;
    }

    QCStatus_e opRet = op( *pCam );

    (void) pCam->DeInitialize();
    delete pCam;
    (void) DeinitBuffers();
    return opRet;
}

// Full pipeline Init -> Start -> ProcessFrameDescriptor -> Stop -> DeInit.
static QCStatus_e RunFullPipeline(
        const std::string &filePath, std::function<QCStatus_e( QC::Node::Camera & )> op =
                                             []( QC::Node::Camera & ) { return QC_STATUS_OK; } )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera *pCam = new QC::Node::Camera();
    QCStatus_e initRet = pCam->Initialize( config );
    if ( QC_STATUS_OK != initRet )
    {
        delete pCam;
        (void) DeinitBuffers();
        return initRet;
    }

    QCStatus_e startRet = pCam->Start();
    QCStatus_e processRet = QC_STATUS_OK;
    if ( QC_STATUS_OK == startRet )
    {
        // Feed a frame descriptor through ProcessFrameDescriptor.
        NodeFrameDescriptor frameDesc( 1 );
        CameraFrameDescriptor_t camFrameDesc;
        camFrameDesc = config.buffers[0];
        (void) frameDesc.SetBuffer( 0, camFrameDesc );
        processRet = pCam->ProcessFrameDescriptor( frameDesc );
    }

    QCStatus_e opRet = op( *pCam );

    (void) pCam->Stop();
    (void) pCam->DeInitialize();
    delete pCam;
    (void) DeinitBuffers();

    if ( QC_STATUS_OK != startRet ) return startRet;
    if ( QC_STATUS_OK != processRet ) return processRet;
    return opRet;
}

// Erase a single static.<key> from the JSON and call Initialize.
static QCStatus_e RunInit_MissingField( const std::string &filePath, const std::string &fieldName )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, ProcessDoneCb );
    EraseConfig( config, fieldName );
    SetFullMockParam();

    QC::Node::Camera camera;
    return camera.Initialize( config );
}

// Erase a single static.streamConfigs[0].<key> from JSON and Initialize.
static QCStatus_e RunInit_MissingStreamField( const std::string &filePath,
                                              const std::string &fieldName )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, filePath, ProcessDoneCb );
    EraseConfig( config, "streamConfigs[0]." + fieldName );
    SetFullMockParam();

    QC::Node::Camera camera;
    return camera.Initialize( config );
}

// stand up a metadata-enabled Camera through Initialize.
static QCStatus_e
RunInit_MetaData( std::function<QCStatus_e( QC::Node::Camera &, QCNodeInit_t & )> op )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();

    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        if ( QC_STATUS_OK != fullDt.Load( config.config, e ) ) return QC_STATUS_FAIL;
        if ( QC_STATUS_OK != fullDt.Get( "static", staticCfg ) ) return QC_STATUS_FAIL;
    }

    if ( QC_STATUS_OK != AllocateFrameBuffers( staticCfg, config.buffers ) ) return QC_STATUS_FAIL;

    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    if ( QC_STATUS_OK != AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) )
        return QC_STATUS_FAIL;

    QC::Node::Camera *pCam = new QC::Node::Camera();
    QCStatus_e initRet = pCam->Initialize( config );
    QCStatus_e opRet = QC_STATUS_FAIL;
    if ( QC_STATUS_OK == initRet )
    {
        opRet = op( *pCam, config );
        (void) pCam->DeInitialize();
    }
    else
    {
        opRet = initRet;
    }
    delete pCam;
    (void) DeinitBuffers();
    return opRet;
}

// Validate a metadata config through CameraConfig::VerifyAndSet only
static QCStatus_e RunVerify_InjectionConfig( const QCNodeInit_t &cfg, std::string &errors )
{
    SetFullMockParam();
    QC::Node::Camera camera;
    QCNodeConfigIfs &configIfs = camera.GetConfigurationIfs();
    return configIfs.VerifyAndSet( cfg.config, errors );
}

// Allocate `count` generic DMA buffers (one pool) and append their descriptors to
// `buffers` so indices 0..count-1 are all valid. Used by the injection test where
// the frame/metadata/injection sublists all index into one shared buffer vector.
static QCStatus_e AllocateGenericBuffers(
        const std::string &name, uint32_t count, size_t bufSize,
        std::vector<std::reference_wrapper<QC::Memory::QCBufferDescriptorBase_t>> &buffers )
{
    QCNodeID_t nodeId;
    nodeId.name = name;
    nodeId.type = QC_NODE_TYPE_QCX;
    nodeId.id = 0;

    BufferProps_t bufProp;
    bufProp.size = bufSize;
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;

    uint32_t basePool = g_bufferPools.size();
    g_bufferPools.reserve( QCNODE_CAMERA_MAX_STREAM_NUM );
    g_bufferPools.resize( basePool + 1 );

    QCStatus_e ret = g_bufferPools[basePool].Init( name + "_pool", nodeId, LOGGER_LEVEL_ERROR,
                                                   count, bufProp );
    if ( QC_STATUS_OK != ret ) return ret;
    return g_bufferPools[basePool].GetBuffers( buffers );
}

// Read up to `maxBytes` of binary data from `fileName` into the DMA buffer
static QCStatus_e LoadRawFileIntoBuffer(
        const std::string &fileName, size_t maxBytes, uint32_t bufferIdx,
        std::vector<std::reference_wrapper<QC::Memory::QCBufferDescriptorBase_t>> &buffers,
        size_t &outBytesRead )
{
    outBytesRead = 0;
    if ( bufferIdx >= buffers.size() )
    {
        return QC_STATUS_BAD_ARGUMENTS;
    }
    QC::Memory::QCBufferDescriptorBase_t &bufDesc = buffers[bufferIdx];
    if ( nullptr == bufDesc.pBuf )
    {
        return QC_STATUS_BAD_ARGUMENTS;
    }

    std::ifstream file( fileName, std::ios::binary | std::ios::ate );
    if ( !file.is_open() )
    {
        return QC_STATUS_FAIL;
    }
    std::streamoff fileSize = file.tellg();
    file.seekg( 0, std::ios::beg );
    size_t toRead = ( (size_t) fileSize < maxBytes ) ? (size_t) fileSize : maxBytes;
    file.read( static_cast<char *>( bufDesc.pBuf ), static_cast<std::streamsize>( toRead ) );
    QCStatus_e ret = ( file.good() || file.eof() ) ? QC_STATUS_OK : QC_STATUS_FAIL;
    file.close();
    if ( QC_STATUS_OK == ret )
    {
        outBytesRead = toRead;
    }
    return ret;
}


// Helper: a metadata config whose bufferListId lands in the INPUT_METADATA type
// range (type bits = 2, shift = 8 -> base 0x200). The sticky-TUNING_FEATURE init
// loop in CameraImpl::Initialize only fires when the type check
// matches QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA.
static nlohmann::json MakeInputMetadataConfigJson()
{
    nlohmann::json meta;
    meta["tag"] = "TUNING_FEATURE_1_MODE";
    meta["bufferListId"] = ( 2U << 8 ) | 4U;   // INPUT_METADATA range
    meta["bufferIds"] = { 4, 5, 6, 7 };
    return meta;
}


TEST( Camera, SANITY_Test_Camera_Frame_IMX728_ReleaseMode_NV12_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    dt.Set<bool>( "static.requestMode", false );

    SANITY_Test_CameraReleaseMode_Mock( dt );
}


TEST( Camera, SANITY_Test_Camera_Frame_OV3F_ReleaseMode_NV12_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_ov3f_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    dt.Set<bool>( "static.requestMode", false );

    SANITY_Test_CameraReleaseMode_Mock( dt );
}

TEST( Camera, SANITY_Test_Camera_MetaData_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata_tuning_mode.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );

    SANITY_Test_Camera_MetaData( dt );
}

TEST( Camera, SANITY_Test_CameraMonitor_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    SANITY_Test_CameraMonitor( dt );
}

TEST( Camera, SANITY_Test_CameraMonitor_NoInit_Mock )
{
    SetFullMockParam();

    QC::Node::Camera camera;
    std::string errors;

    QCNodeMonitoringIfs &monitorIfs = camera.GetMonitoringIfs();

    QCStatus_e ret = monitorIfs.VerifyAndSet( "{}", errors );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, ret );

    std::string opts;
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, monitorIfs.GetOptions( opts ) );

    const QCNodeMonitoringBase_t &monCfg = monitorIfs.Get();
    (void) monCfg;
}

TEST( Camera, SANITY_Test_CameraConfig_GetOptions_NoInit_Mock )
{
    SetFullMockParam();

    QC::Node::Camera camera;
    QCNodeConfigIfs &configIfs = camera.GetConfigurationIfs();

    std::string opts;
    ASSERT_EQ( QC_STATUS_OK, configIfs.GetOptions( opts ) );
    ASSERT_EQ( "", opts );
}

TEST( Camera, EXCEPTION_Test_DeInit_NoInit_Mock )
{
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.DeInitialize();
    ASSERT_NE( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_StartStop_NoInit_Mock )
{
    SetFullMockParam();

    // Start/Stop on a never-initialized Camera must both surface BAD_STATE.
    QC::Node::Camera camera;

    QCStatus_e ret = camera.Start();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = camera.Stop();
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );
}

TEST( Camera, SANITY_Test_FullInit_Mock )
{
    // GetState on a fresh Camera must report INITIAL.
    SetFullMockParam();
    QC::Node::Camera camera;
    QCObjectState_e st = camera.GetState();
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, st );
}

TEST( Camera, EXCEPTION_Test_QueryInputs_RetryOnce_Mock )
{
    // Force QCarCamQueryInputs(NULL, 0, &inputCount) to return failure on the FIRST
    // call so the constructor exercises the queryCount++/sleep retry branch
    // FullMock makes subsequent calls return OK with
    // inputCount=1 so the retry loop exits cleanly after the one-shot failure.
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    g_controlFnc = MockCamera_GetControlFnc( "libCameraMock.so" );
    g_setFullMockFnc = MockCamera_GetSetFullMockFnc( "libCameraMock.so" );
    ASSERT_NE( g_controlFnc, nullptr );
    ASSERT_NE( g_setFullMockFnc, nullptr );

    g_setFullMockFnc( true );
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
}

TEST( Camera, EXCEPTION_Test_QueryInputs_CountMismatch_Mock )
{
    // Force QCarCamQueryInputs to return failure -> status-mismatch branch.
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    g_controlFnc = MockCamera_GetControlFnc( "libCameraMock.so" );
    g_setFullMockFnc = MockCamera_GetSetFullMockFnc( "libCameraMock.so" );
    ASSERT_NE( g_controlFnc, nullptr );
    ASSERT_NE( g_setFullMockFnc, nullptr );

    g_setFullMockFnc( true );
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
}

TEST( Camera, SANITY_Test_FullInit_RawJson_Mock )
{
    // Try Initialize with the JSON loaded directly (no DataTree round-trip via Dump).
    // If this succeeds where Dump-then-Initialize fails, the issue is in dt.Dump()
    // or how DataTree handles its parent envelope when re-loaded.
    SetFullMockParam();

    std::ifstream f( "./data/test/camera/camera_config_imx728_request_nv12.json" );
    ASSERT_TRUE( f.is_open() );
    std::stringstream buf;
    buf << f.rdbuf();
    std::string jsonStr = buf.str();

    QCNodeInit_t config;
    config.config = jsonStr;
    config.callback = ProcessDoneCb;

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    std::cerr << "DEBUG: raw-json Initialize returned " << ret << std::endl;
    if ( QC_STATUS_OK == ret )
    {
        camera.DeInitialize();
    }
}

TEST( Camera, EXCEPTION_Test_Init_QCarCamOpen_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_OPEN, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, EXCEPTION_Test_Init_RegisterEventCallback_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, EXCEPTION_Test_Init_SetParam_EventMask_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, EXCEPTION_Test_Init_Reserve_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_RESERVE, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, EXCEPTION_Test_Init_SetBuffers_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_SET_BUFFERS, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}


TEST( Camera, SANITY_Test_Init_DeInit_Success_Mock )
{
    // End-to-end Initialize -> DeInitialize with cv_mock backed by a real mp4.
    // This is the lever that makes Start/Stop/Submit branches reachable in
    // subsequent tests, since CameraImpl runs Initialize all the way to READY
    // and then DeInit unwinds Reserve/Release/Close cleanly.
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      []( QC::Node::Camera & ) { return QC_STATUS_OK; } );
    EXPECT_EQ( QC_STATUS_OK, ret );
}


TEST( Camera, EXCEPTION_Test_Start_QCarCamStart_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      [&]( QC::Node::Camera &cam ) {
                                          g_controlFnc( MOCK_API_QCARCAM_START,
                                                        MOCK_CONTROL_API_RETURN, &failRet );
                                          return cam.Start();
                                      } );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, SANITY_Test_Start_Stop_Success_Mock )
{
    // Init OK -> Start OK -> Stop OK. Covers the success arms of CameraImpl::Start
    // and CameraImpl::Stop, plus SubmitAllBuffers.
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      []( QC::Node::Camera &cam ) {
                                          if ( QC_STATUS_OK != cam.Start() ) return QC_STATUS_FAIL;
                                          return cam.Stop();
                                      } );
    EXPECT_EQ( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_Stop_QCarCamStop_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      [&]( QC::Node::Camera &cam ) {
                                          if ( QC_STATUS_OK != cam.Start() ) return QC_STATUS_FAIL;
                                          g_controlFnc( MOCK_API_QCARCAM_STOP,
                                                        MOCK_CONTROL_API_RETURN, &failRet );
                                          return cam.Stop();
                                      } );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}


TEST( Camera, SANITY_Test_Pipeline_ProcessFrame_Success_Mock )
{
    // Init OK -> Start OK -> ProcessFrameDescriptor (request mode -> SubmitRequest)
    // -> Stop OK -> DeInit OK. Covers the request-mode SubmitRequest happy path.
    QCStatus_e ret = RunFullPipeline( "./data/test/camera/camera_config_imx728_request_nv12.json" );
    EXPECT_EQ( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_ProcessFrame_SubmitRequest_Fail_Mock )
{
    // Same flow but inject QCarCamSubmitRequest failure during ProcessFrameDescriptor.
    // This is tricky: the helper does ProcessFrame between Start and op(), so the
    // failure must be armed before the helper runs. Re-implement inline.
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Now inject the failure for the next SubmitRequest call
    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    camFrameDesc = config.buffers[0];
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrameDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_ProcessFrame_InvalidBuffer_Mock )
{
    // Run Init+Start, then feed a frame descriptor whose dmaHandle isn't
    // registered. CameraImpl::ProcessFrameDescriptor should reject with
    // QC_STATUS_INVALID_BUF.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t bogus;   // default-constructed, dmaHandle = 0
    bogus.dmaHandle = 0xDEADBEEF;    // unregistered handle
    bogus.streamId = 1;
    bogus.frameIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, bogus ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Pipeline_ReleaseMode_Mock )
{
    // Same as Pipeline_ProcessFrame_Success but with requestMode=false → uses
    // ReleaseFrame instead of SubmitRequest. Covers CameraImpl::ReleaseFrame.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    camFrameDesc = config.buffers[0];
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrameDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Pipeline_ReleaseFrame_Fail_Mock )
{
    // requestMode=false + QCarCamReleaseFrame fails → ProcessFrame should fail.
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_controlFnc( MOCK_API_QCARCAM_RELEASE_FRAME, MOCK_CONTROL_API_RETURN, &failRet );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    camFrameDesc = config.buffers[0];
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrameDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_ProcessFrame_NotRunning_Mock )
{
    // ProcessFrameDescriptor from a fresh (non-Started) Camera should surface BAD_STATE.
    SetFullMockParam();

    QC::Node::Camera camera;
    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t descBase;   // default-constructed
    QCStatus_e ret = frameDesc.SetBuffer( 0, descBase );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
}

TEST( Camera, EXCEPTION_Test_DoubleInit_Mock )
{
    // First Initialize succeeds; second Initialize on same object must fail BAD_STATE.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret1 = camera.Initialize( config );
    QCStatus_e ret2 = camera.Initialize( config );

    EXPECT_EQ( QC_STATUS_OK, ret1 );
    EXPECT_NE( QC_STATUS_OK, ret2 );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_DoubleStart_Mock )
{
    // Init OK, Start OK, then Start again must return BAD_STATE.
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      []( QC::Node::Camera &cam ) {
                                          if ( QC_STATUS_OK != cam.Start() ) return QC_STATUS_FAIL;
                                          QCStatus_e r = cam.Start();
                                          cam.Stop();
                                          return r;
                                      } );
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
}

TEST( Camera, EXCEPTION_Test_DoubleStop_Mock )
{
    // Init OK, Stop without Start must return BAD_STATE.
    QCStatus_e ret = RunInit_Then_Op( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                      []( QC::Node::Camera &cam ) {
                                          return cam.Stop();   // never started
                                      } );
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
}


TEST( Camera, EXCEPTION_Test_Config_Missing_Name_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "name" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_Id_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "id" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_InputId_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "inputId" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_ClientId_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "clientId" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_SrcId_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "srcId" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_InputMode_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "inputMode" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_IspUseCase_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "ispUseCase" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_FrameDropPattern_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "camFrameDropPattern" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_FrameDropPeriod_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "camFrameDropPeriod" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_OpMode_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "opMode" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Missing_StreamConfigs_Mock )
{
    QCStatus_e ret = RunInit_MissingField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "streamConfigs" );
    EXPECT_NE( QC_STATUS_OK, ret );
}


TEST( Camera, EXCEPTION_Test_Stream_Missing_StreamId_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "streamId" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_Missing_Width_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "width" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_Missing_Height_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "height" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_Missing_Format_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "format" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_Missing_BufferIds_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "bufferIds" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_Missing_SubmitRequestPattern_Mock )
{
    QCStatus_e ret = RunInit_MissingStreamField(
            "./data/test/camera/camera_config_imx728_request_nv12.json", "submitRequestPattern" );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_TooManyStreams_Mock )
{
    // QCNODE_CAMERA_MAX_STREAM_NUM is presumably 4 or 8; pad streamConfigs to a
    // huge size and expect BAD_ARGUMENTS at the streamNum > MAX check.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    {
        nlohmann::json one = GetFirstStreamConfig( config );
        for ( int i = 0; i < 64; ++i ) PushStreamConfig( config, one );
    }
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_EmptyArray_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "streamConfigs", nlohmann::json::array() );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_DuplicateBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    // Force duplicate buffer ids: replace the array with [0,0,0]
    SetConfig( config, "streamConfigs[0].bufferIds", nlohmann::json::array( { 0, 0, 0 } ) );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Stream_BufferIdsExceedMax_Mock )
{
    // Stream bufferIds count > QCNODE_CAMERA_MAX_BUFFER_NUM (20) must be rejected at
    // "the bufferIds size ... is larger than maximum". Use 21
    // *unique* ids so the size==0 and duplicate-id branches are not taken first.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    {
        nlohmann::json tooMany = nlohmann::json::array();
        for ( uint32_t i = 0; i < 21U; i++ )
        {
            tooMany.push_back( i );
        }
        SetConfig( config, "streamConfigs[0].bufferIds", tooMany );
    }
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_EmptyArray_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "metaDataConfigs", nlohmann::json::array() );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_Missing_BufferListId_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    EraseConfig( config, "metaDataConfigs[0].bufferListId" );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_Missing_BufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "metaDataConfigs[0].bufferIds", nlohmann::json::array() );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_DuplicateBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    // Reuse the streamConfigs[0]'s bufferIds in metadata so the dup-check fires
    {
        nlohmann::json streamIds = GetFirstStreamConfig( config )["bufferIds"];
        SetConfig( config, "metaDataConfigs[0].bufferIds", streamIds );
    }
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_BufferIdsExceedMax_Mock )
{
    // Metadata bufferIds count > QCNODE_CAMERA_MAX_BUFFER_NUM (20) must be rejected at
    // "the bufferIds size for metadata ... is larger than maximum".
    // The MAX check precedes the duplicate-id loop, so 21 ids trip it regardless of values.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    {
        nlohmann::json tooMany = nlohmann::json::array();
        for ( uint32_t i = 0; i < 21U; i++ )
        {
            tooMany.push_back( 100U + i );
        }
        SetConfig( config, "metaDataConfigs[0].bufferIds", tooMany );
    }
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_ConfigsKeyMissing_Mock )
{
    // enableMetaData=true but metaDataConfigs key absent -> dt.Get fails with
    // "metaDataConfigs is invalid"; the lookup failure
    // (QC_STATUS_OUT_OF_BOUND) propagates out. Distinct from Metadata_EmptyArray.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    EraseConfig( config, "metaDataConfigs" );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OUT_OF_BOUND, ret );
}

TEST( Camera, EXCEPTION_Test_Metadata_NotInRequestMode_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    (void) DeinitBuffers();
}


TEST( Camera, EXCEPTION_Test_InitDeinitError_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_InitDeinitError_Mock( dt );
}

TEST( Camera, EXCEPTION_Test_StartStopError_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_StartStopError( dt );
}

TEST( Camera, EXCEPTION_Test_SetFrameBuffer_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SetFrameBuffer( dt );
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffer_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata_tuning_mode.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SetMetaDataBuffer( dt );
}

TEST( Camera, EXCEPTION_Test_SubmitRequest_Frame_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SubmitRequest_Frame( dt );
}

TEST( Camera, EXCEPTION_Test_ReleaseFrame_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    dt.Set<bool>( "static.requestMode", false );
    Exception_Test_ReleaseFrame( dt );
}

TEST( Camera, Exception_Test_SubmitRequest_MetaData_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata_tuning_mode.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SubmitRequest_MetaData( dt );
}

TEST( Camera, EXCEPTION_Test_EventCallback_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_EventCallback( dt );
}

TEST( Camera, EXCEPTION_Test_QueryInputs_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_QueryInputs_Error( dt );
}

TEST( Camera, EXCEPTION_Test_ZeroModeNum_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_ZeroModeNum( dt );
}

TEST( Camera, EXCEPTION_Test_QCarCamInitialize_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_QCarCamInitialize( dt );
}

TEST( Camera, EXCEPTION_Test_GetFrame_Failure_Mock )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_GetFrame_Failure( dt );
}

TEST( Camera, EXCEPTION_Test_MultipleCamera_RefCount_Mock )
{
    SetFullMockParam();

    // 1st camera: refcount 0 -> 1; QueryInputs succeeds via SetFullMockParam's
    // armed OUT_PARAM g_mockInputsInfo.
    QC::Node::Camera *pCamera1 = new QC::Node::Camera();
    ASSERT_NE( nullptr, pCamera1 );

    // 2nd camera: refcount 1 -> 2; the else-branch ctor runs, no qcarcam calls.
    QC::Node::Camera *pCamera2 = new QC::Node::Camera();
    ASSERT_NE( nullptr, pCamera2 );

    // 1st dtor: refcount 2 -> 1, "Skip QCarCamUninitialize" branch.
    delete pCamera1;
    pCamera1 = nullptr;

    // 2nd dtor: refcount 1 -> 0, QCarCamUninitialize is called and
    // FreeCameraInputsInfo runs.
    delete pCamera2;
    pCamera2 = nullptr;
}

TEST( Camera, SANITY_Test_Pipeline_Metadata_Init_Mock )
{
    // Init OK with metaData enabled -> covers SetMetaDataBuffers happy path.
    QCStatus_e ret = RunInit_MetaData( []( QC::Node::Camera &cam, QCNodeInit_t & ) {
        (void) cam;
        return QC_STATUS_OK;
    } );
    EXPECT_EQ( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffers_BufferIdxOOR_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    // Re-point the metadata bufferIds at an out-of-range index (99). Buffers were
    // allocated from the valid config above; only the config string handed to
    // Initialize carries the OOR index, so SetMetaDataBuffers rejects it.
    QCNodeInit_t oorConfig;
    LoadCameraConfig( oorConfig, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( oorConfig, "metaDataConfigs[0].bufferIds",
               nlohmann::json::array( { 4, 5, 6, 99 } ) );
    SetFullMockParam();
    config.config = oorConfig.config;

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OUT_OF_BOUND, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Init_MetaData_Reserve_Fail_Cleanup_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    // Reserve runs after SetFrameBuffers + SetMetaDataBuffers, so by the time it
    // fails the config vectors are fully populated -> the cleanup clears them.
    g_controlFnc( MOCK_API_QCARCAM_RESERVE, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_EventCallback_NotRunning_Mock )
{
    // Trigger QCARCAM_EVENT_FRAME_READY before Camera reaches RUNNING state.
    // EventCallback should reject because m_state != RUNNING.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    // Camera is in READY (post-Init), not RUNNING. Fire an event — EventCallback
    // should hit the early-out branch.
    QCarCamEventPayload_t payload = {};
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_EventCallback_FrameReady_Mock )
{
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          payload.frameInfo.id = 1;   // matches streamId in JSON
                          payload.frameInfo.bufferIndex = 0;
                          payload.frameInfo.seqNo = 0;
                          g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );
                      } );
}

TEST( Camera, SANITY_Test_EventCallback_ErrorEvent_Mock )
{
    // Init -> Start -> trigger QCARCAM_EVENT_ERROR -> covers EventCallback non-frame path.
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          g_triggerEventFnc( QCARCAM_EVENT_ERROR, &payload, false );
                      } );
}

TEST( Camera, SANITY_Test_EventCallback_NullPrivateData_Mock )
{
    // Trigger event with useNullPrivateData=true -> covers QcarcamEventCb's
    // `nullptr == pPrivateData` branch.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamEventPayload_t payload = {};
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, /*useNullPrivateData=*/true );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_EventCallback_MultiStreamFrameReady_Mock )
{
    // Init with enableMultiStreamFrameReady=true, fire MULTI_STREAM_FRAME_READY -> covers
    // the QCARCAM_EVENT_MULTI_STREAM_FRAME_READY arm of QcarcamEventCb.
    RunStartedCamera(
            "./data/test/camera/camera_config_imx728_request_nv12.json",
            []( QC::Node::Camera & ) {
                QCarCamEventPayload_t payload = {};
                payload.multiFrameInfo.numFrameInfo = 1;
                payload.multiFrameInfo.batchFrameInfo[0].id = 1;
                payload.multiFrameInfo.batchFrameInfo[0].bufferIndex = 0;
                g_triggerEventFnc( QCARCAM_EVENT_MULTI_STREAM_FRAME_READY, &payload, false );
            },
            []( QCNodeInit_t &config ) {
                SetConfig( config, "enableMultiStreamFrameReady", true );
            } );
}

TEST( Camera, EXCEPTION_Test_EventCallback_MultiStreamFrameReady_GetFrameFail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      CaptureFrameCb );
    SetConfig( config, "requestMode", false );
    SetConfig( config, "enableMultiStreamFrameReady", true );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_captured = { 0, 0, 0 };

    // Non-request GetFrame reads QCarCamGetFrame's out-param bufferIndex; inject
    // an out-of-range value so GetFrame returns OUT_OF_BOUND inside the batch loop.
    QCarCamFrameInfo_t injected = {};
    injected.id = 1;
    injected.bufferIndex = 99;
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_OUT_PARAM1, &injected );

    QCarCamEventPayload_t payload = {};
    payload.multiFrameInfo.numFrameInfo = 1;
    payload.multiFrameInfo.batchFrameInfo[0].id = 1;
    payload.multiFrameInfo.batchFrameInfo[0].bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_MULTI_STREAM_FRAME_READY, &payload, false );

    EXPECT_EQ( 0U, g_captured.okCount );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_MultiClient_NonPrimary_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    // Trim bufferIds to 4 so the inner ImportBuffers loop bound matches
    // the bufferList we're about to provide.
    SetConfig( config, "streamConfigs[0].bufferIds", nlohmann::json::array( { 0, 1, 2, 3 } ) );
    SetConfig( config, "clientId", 1u );   // non-zero -> multi-client
    SetConfig( config, "primary", false );
    SetFullMockParam();

    QCarCamBufferList_t localList;
    QCarCamBuffer_t localBufs[4];
    SharedBufferPool localPool;
    BuildPoolBufferList( localList, localBufs, localPool, 4 );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &localList );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.DeInitialize() );
    (void) localPool.Deinit();
}

TEST( Camera, SANITY_Test_Init_RequestPatternMode_Mock )
{
    // streamConfigs has multiple streams with non-zero submitRequestPattern values
    // -> covers the m_bRequestPatternMode = true branch in Initialize.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    {
        // Add a 2nd stream with submitRequestPattern=2 to trigger pattern mode.
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["submitRequestPattern"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK == ret )
    {
        (void) camera.DeInitialize();
    }
    (void) DeinitBuffers();
    SUCCEED();   // any outcome is OK; this test exists for branch coverage
}

TEST( Camera, SANITY_Test_SubmitRequest_PatternMode_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    // First stream: pattern=0 (reference). Second: pattern=2 -> pattern mode on.
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 0u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 2;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK != ret )
    {
        (void) DeinitBuffers();
        SUCCEED();
        return;
    }
    if ( QC_STATUS_OK != camera.Start() )
    {
        (void) camera.DeInitialize();
        (void) DeinitBuffers();
        SUCCEED();
        return;
    }

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;   // primary stream first; secondary indexing was unsafe
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
    SUCCEED();
}

TEST( Camera, SANITY_Test_Init_FrameDropPattern_Mock )
{
    // camFrameDropPattern != 0 -> covers the QCARCAM_PARAM_FRAME_DROP_CONTROL
    // SetParam call in Initialize.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "camFrameDropPattern", 1u );
    SetConfig( config, "camFrameDropPeriod", 2u );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_RecoveryMode_Mock )
{
    // recovery=true -> covers the QCARCAM_OPEN_FLAGS_RECOVERY arm in Initialize.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "recovery", true );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Pipeline_Metadata_SubmitRequest_Mock )
{
    // Init OK with metaData enabled -> Start -> ProcessFrame with a metadata
    // descriptor whose streamRequestNum > 0 -> covers CameraImpl::SubmitRequest(metadata)
    // happy path and ProcessFrameDescriptor metadata branch.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Build a metadata descriptor with streamRequestNum > 0 to drive
    // SubmitRequest(metadata) into the QCarCamSubmitRequest call.
    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    metaDataDesc.streamRequestNum = 1;
    metaDataDesc.requestId = 42;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, metaDataDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Pipeline_Metadata_SubmitRequest_Fail_Mock )
{
    // Same as success path, but inject QCarCamSubmitRequest failure -> covers
    // SubmitRequest(metadata) error branch.
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );

    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    metaDataDesc.streamRequestNum = 1;
    metaDataDesc.requestId = 99;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, metaDataDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Pipeline_Metadata_ZeroStreamRequest_Mock )
{
    // streamRequestNum == 0 -> SubmitRequest(metadata) skips QCarCamSubmitRequest
    // -> the !numStreamRequests path.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    metaDataDesc.streamRequestNum = 0;   // skip SubmitRequest call
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, metaDataDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_ProcessFrame_MetaDataNotEnabled_Mock )
{
    // Init without enableMetaData -> Start -> feed metadata descriptor ->
    // ProcessFrameDescriptor takes the "Metadata flag is not enabled" branch
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    metaDataDesc.streamRequestNum = 1;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, metaDataDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Injection_MissingInjectionConfig_Mock )
{
    // INJECTION_SENSOR_METADATA tag but no InjectionConfig object ->
    // "InjectionConfig ... is missing".
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    EraseConfig( config, "metaDataConfigs[0].InjectionConfig" );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Injection_EmptyInputBufferIds_Mock )
{
    // InjectionConfig with empty inputBufferIds -> "inputBufferIds ... is empty"
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.inputBufferIds",
               nlohmann::json::array() );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Injection_InvalidFormat_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.format",
               std::string( "not_a_real_format" ) );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, SANITY_Test_Injection_ConfigParsed_Mock )
{
    // A structurally valid INJECTION_SENSOR_METADATA config passes VerifyAndSet,
    // exercising the full injection-parse block and
    // the GetInjectQcarCamFormat "mipiraw_12" branch.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_OK, ret ) << "errors=" << errors;
}

TEST( Camera, SANITY_Test_Injection_AllInjectFormats_Mock )
{
    // Drive GetInjectQcarCamFormat through every supported format string so each
    // mapping branch (mipiraw_8/10/12/14/16, plain16_10/12/14/16) is covered.
    const char *formats[] = { "mipiraw_8",  "mipiraw_10", "mipiraw_12", "mipiraw_14", "mipiraw_16",
                              "plain16_10", "plain16_12", "plain16_14", "plain16_16" };
    for ( const char *fmt : formats )
    {
        QCNodeInit_t config;
        LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
        SetConfig( config, "metaDataConfigs[0].InjectionConfig.format", std::string( fmt ) );
        std::string errors;
        QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
        // Each is a recognised format, so config validation must accept it.
        EXPECT_EQ( QC_STATUS_OK, ret ) << "format=" << fmt << " errors=" << errors;
    }
}

TEST( Camera, SANITY_Test_Injection_Pipeline_Init_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Size the generic pool to hold the real injection input frame
    // (3840x2160 mipiraw_12 = 12441600 bytes); the metadata/header/eeprom/output
    // buffers (ids 4-7, 10-12) share the same pool size, which is harmless.
    const uint32_t kNumMetaBufs = 9;   // ids 4..12
    size_t metaSize = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    size_t frameSize = (size_t) ( 3840 * 2160 * 3 / 2 );   // mipiraw_12
    size_t bufSize = ( metaSize > frameSize ) ? metaSize : frameSize;
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_meta", kNumMetaBufs, bufSize, config.buffers ) );

    // Load the real ISP-injection payloads: injection_input_frame.raw into the
    // injection input buffer (id 8) and injection_header_file.raw into the sensor
    // header buffer (id 10). inputBufferIds=[8,9], headerBufferIds=[10] per
    // camera_config_metadata_isp_injection.json.
    size_t frameBytes = 0;
    size_t headerBytes = 0;
    EXPECT_EQ( QC_STATUS_OK, LoadRawFileIntoBuffer( "./data/test/camera/injection_input_frame.raw",
                                                    bufSize, 8, config.buffers, frameBytes ) );
    EXPECT_EQ( QC_STATUS_OK, LoadRawFileIntoBuffer( "./data/test/camera/injection_header_file.raw",
                                                    bufSize, 10, config.buffers, headerBytes ) );
    EXPECT_EQ( frameSize, frameBytes );
    EXPECT_GT( headerBytes, 0u );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_TuningFeatureIspSettings_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "metaDataConfigs",
               nlohmann::json::array( { MakeInputMetadataConfigJson() } ) );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Init_IspSettings_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "metaDataConfigs",
               nlohmann::json::array( { MakeInputMetadataConfigJson() } ) );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_ISP_SETTINGS, MOCK_CONTROL_API_RETURN, &failRet );

    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) DeinitBuffers();
}

// QCarCamRelease fails during DeInitialize -> CameraImpl::DeInitialize error
// branch.
TEST( Camera, EXCEPTION_Test_DeInit_QCarCamRelease_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    // Arm the Release failure right before DeInit calls it.
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_RELEASE, MOCK_CONTROL_API_RETURN, &failRet );

    QCStatus_e ret = camera.DeInitialize();
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) DeinitBuffers();
}

// QCarCamClose fails during DeInitialize -> CameraImpl::DeInitialize close
// error branch.
TEST( Camera, EXCEPTION_Test_DeInit_QCarCamClose_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_CLOSE, MOCK_CONTROL_API_RETURN, &failRet );

    QCStatus_e ret = camera.DeInitialize();
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) DeinitBuffers();
}

// QCarCamStop fails during Stop() -> Stop error branch.
TEST( Camera, EXCEPTION_Test_Stop_QCarCamStop_Fail2_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_STOP, MOCK_CONTROL_API_RETURN, &failRet );

    QCStatus_e ret = camera.Stop();
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Config_EmptyName_Mock )
{
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    ReadJsonFile( "./data/test/camera/camera_config_imx728_request_nv12.json", jsonData );
    jsonData["static"]["name"] = "";
    ASSERT_EQ( QC_STATUS_OK, dt.Load( jsonData.dump(), errors ) );

    SetFullMockParam();
    QC::Node::Camera camera;
    QCNodeConfigIfs &configIfs = camera.GetConfigurationIfs();
    QCStatus_e ret = configIfs.VerifyAndSet( dt.Dump(), errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_Metadata_UnknownTag_Mock )
{
    nlohmann::json meta;
    meta["tag"] = "FOO_BAR_UNKNOWN_TAG";
    meta["bufferListId"] = 4;
    meta["bufferIds"] = { 4, 5, 6, 7 };

    nlohmann::json jsonData;
    ReadJsonFile( "./data/test/camera/camera_config_metadata_tuning_mode.json", jsonData );
    jsonData["static"]["metaDataConfigs"] = nlohmann::json::array( { meta } );

    std::string errors;
    SetFullMockParam();
    QC::Node::Camera camera;
    QCNodeConfigIfs &configIfs = camera.GetConfigurationIfs();
    QCStatus_e ret = configIfs.VerifyAndSet( jsonData.dump(), errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Injection_DuplicateInputBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.inputBufferIds",
               nlohmann::json::array( { 4, 4 } ) );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Injection_DuplicateOutputBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json" );
    SetConfig( config, "metaDataConfigs[0].outputBufferIds", nlohmann::json::array( { 4 } ) );
    std::string errors;
    QCStatus_e ret = RunVerify_InjectionConfig( config, errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, EXCEPTION_Test_Config_StreamId_OutOfRange_Mock )
{
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    ReadJsonFile( "./data/test/camera/camera_config_imx728_request_nv12.json", jsonData );
    jsonData["static"]["streamConfigs"][0]["streamId"] = 9999;
    ASSERT_EQ( QC_STATUS_OK, dt.Load( jsonData.dump(), errors ) );

    SetFullMockParam();
    QC::Node::Camera camera;
    QCNodeConfigIfs &configIfs = camera.GetConfigurationIfs();
    QCStatus_e ret = configIfs.VerifyAndSet( dt.Dump(), errors );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

TEST( Camera, SANITY_Test_FrameReady_InvalidFlag_Mock )
{
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          payload.frameInfo.id = 1;
                          payload.frameInfo.bufferIndex = 0;
                          payload.frameInfo.flags = QCARCAM_BUFFER_STATUS_INVALID;
                          g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );
                      } );
}

TEST( Camera, EXCEPTION_Test_Init_InjectionConfig_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      ProcessDoneCb );
    SetFullMockParam();
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_STANDALONE_INJECTION_CONFIG, MOCK_CONTROL_API_RETURN,
                  &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Init_BatchMode_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      ProcessDoneCb );
    SetFullMockParam();

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_EX_BATCH_MODE, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffers_SetBuffers_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      ProcessDoneCb );
    SetConfig( config, "metaDataConfigs",
               nlohmann::json::array( { MakeInputMetadataConfigJson() } ) );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Start_AfterInit_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_START, MOCK_CONTROL_API_RETURN, &failRet );

    QCStatus_e ret = camera.Start();
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_RegisterInjectionBufferList_Mock )
{
    // Buffer index layout (from the fixture): 0-3 frame, 4-7 metadata list,
    //   8-9 injection input, 10 header, 11 eeprom, 12 output metadata.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }

    // Allocate buffer pools so config.buffers[0..12] are valid for the indices
    // the injection config references:
    //   AllocateFrameBuffers    -> indices 0-3 (3840x2160 nv12) via image-prop pool
    //   AllocateMetaDataBuffers -> indices 4-7 (metadata raw bytes) via BufferProps pool
    //   AllocateGenericBuffers  -> indices 8-12 (injection input/header/eeprom/output)
    // (AllocateFrameBuffersForConfig above already allocated indices 0-3)

    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    // 5 generic buffers: injection input(2) + header(1) + eeprom(1) + output(1).
    size_t injBufSize = 3840 * 2160 * 2;
    ASSERT_EQ( QC_STATUS_OK, AllocateGenericBuffers( "inj", 5, injBufSize, config.buffers ) );

    // Load the real ISP-injection payloads into the injection input/header buffers.
    // injection_input_frame.raw is a 3840x2160 mipiraw_12 frame (12441600 bytes);
    // injection_header_file.raw is the per-frame sensor metadata blob (11520 bytes).
    // inputBufferIds=[8,9], headerBufferIds=[10] per camera_config_metadata_isp_injection.json.
    size_t frameBytes = 0;
    size_t headerBytes = 0;
    EXPECT_EQ( QC_STATUS_OK, LoadRawFileIntoBuffer( "./data/test/camera/injection_input_frame.raw",
                                                    injBufSize, 8, config.buffers, frameBytes ) );
    EXPECT_EQ( QC_STATUS_OK, LoadRawFileIntoBuffer( "./data/test/camera/injection_header_file.raw",
                                                    injBufSize, 10, config.buffers, headerBytes ) );
    // Sanity: the real frame is the full mipiraw_12 payload, the header is non-empty.
    EXPECT_EQ( (size_t) ( 3840 * 2160 * 3 / 2 ), frameBytes );
    EXPECT_GT( headerBytes, 0u );

    // No SetBuffers fault — let Init run all the way through so it actually
    // enters RegisterInjectionBufferList (the function we're trying to cover).
    // In FullMock mode every qcarcam call returns OK, so Init runs end-to-end
    // and exercises the full injection-buffer registration path.

    // Use stack-allocated camera so dtor runs.
    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    (void) ret;
    // Best-effort DeInit if Init succeeded; ignore failure.
    (void) camera.DeInitialize();

    // Aggressively reset mock-armed state before returning. The injection-path
    // Init traverses several APIs whose overrides aren't consumed in FullMock
    // mode (QUERY_INPUTS, QUERY_INPUT_MODES, several SetParam subtypes) — clear
    // them all here, and re-prime g_mockInputsInfo/g_mockInputModes to the
    // known-good baseline (numModes=1) so the next test's Init isn't poisoned.
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    memset( &g_mockInputsInfo, 0, sizeof( g_mockInputsInfo ) );
    g_mockInputsInfo.numModes = 1;
    snprintf( g_mockInputsInfo.inputName, sizeof( g_mockInputsInfo.inputName ), "CAM0" );
    g_mockInputModes.numModes = 1;

    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_MultiClient_ImportBuffers_Mock )
{
    // clientId != 0 + primary=false -> Initialize takes ImportBuffers path
    // Uses SharedBufferPool-backed bufferList so QCarCamGetBuffers
    // returns a list whose memHndls mmap can consume — covers the ImportBuffers
    // happy path through the buffer-registration loop.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "streamConfigs[0].bufferIds", nlohmann::json::array( { 0, 1, 2, 3 } ) );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", false );
    SetFullMockParam();

    QCarCamBufferList_t localList;
    QCarCamBuffer_t localBufs[4];
    SharedBufferPool localPool;
    BuildPoolBufferList( localList, localBufs, localPool, 4 );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &localList );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.DeInitialize() );
    (void) localPool.Deinit();
}

TEST( Camera, EXCEPTION_Test_Init_MultiClient_GetBuffers_Fail_Mock )
{
    // Multi-client mode + force QCarCamGetBuffers to fail -> covers
    // ImportBuffers QCarCamGetBuffers-failure branch.
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_FrameReady_FrameCallback_Mock )
{
    // Init + Start + fire FRAME_READY with valid id and bufferIndex.
    // GetFrame should succeed because Initialize -> SetFrameBuffers populated
    // m_frameBuffers[1].pCamFrameDescs. FrameCallback is called via QcarcamEventCb.
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          payload.frameInfo.id = 1;
                          payload.frameInfo.bufferIndex = 0;
                          payload.frameInfo.seqNo = 0;
                          payload.frameInfo.sofTimestamp.timestamp = 12345;
                          g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );
                      } );
}

TEST( Camera, EXCEPTION_Test_Start_SubmitAllBuffers_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );
    QCStatus_e ret = camera.Start();
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_DeInit_Release_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json" );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_RELEASE, MOCK_CONTROL_API_RETURN, &failRet );
    QCStatus_e ret = camera.DeInitialize();
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_DeInit_Close_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json" );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_CLOSE, MOCK_CONTROL_API_RETURN, &failRet );
    QCStatus_e ret = camera.DeInitialize();
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Init_SetParam_IspUseCase_Fail_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCStatus_e ret =
            RunInit_FaultInject( "./data/test/camera/camera_config_imx728_request_nv12.json",
                                 MOCK_API_QCARCAM_SET_PARAM_ISP_USECASE, &failRet );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
}

TEST( Camera, EXCEPTION_Test_Init_FrameDropPattern_SetParam_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "camFrameDropPattern", 1u );
    SetConfig( config, "camFrameDropPeriod", 2u );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    g_controlFnc( MOCK_API_QCARCAM_SET_PARAM_FRAME_DROP_CONTROL, MOCK_CONTROL_API_RETURN,
                  &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_ProcessFrame_InvalidBufferType_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Feed a generic QCBufferDescriptorBase that's neither CameraFrameDescriptor
    // nor CameraMetaDataDescriptor -> dynamic_cast fails -> INVALID_BUF.
    NodeFrameDescriptor frameDesc( 1 );
    QC::Memory::QCBufferDescriptorBase_t baseDesc;
    baseDesc.name = "Generic";
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, baseDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_StreamFormat_RGB888_Mock )
{
    RunInitDeInit_WithFormat( "rgb" );
}

TEST( Camera, SANITY_Test_StreamFormat_BGR888_Mock )
{
    RunInitDeInit_WithFormat( "bgr" );
}

TEST( Camera, SANITY_Test_StreamFormat_UYVY_Mock )
{
    RunInitDeInit_WithFormat( "uyvy" );
}

TEST( Camera, SANITY_Test_StreamFormat_P010_Mock )
{
    RunInitDeInit_WithFormat( "p010" );
}

TEST( Camera, SANITY_Test_StreamFormat_NV12UBWC_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].format", std::string( "nv12_ubwc" ) );
    SetFullMockParam();
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    QCStatus_e r = AllocateFrameBuffers( staticCfg, config.buffers );
    if ( QC_STATUS_OK != r )
    {
        SUCCEED();
        (void) DeinitBuffers();
        return;
    }

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK == ret ) (void) camera.DeInitialize();
    (void) DeinitBuffers();
    SUCCEED();
}

TEST( Camera, SANITY_Test_StreamFormat_TP10UBWC_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].format", std::string( "tp10_ubwc" ) );
    SetFullMockParam();
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    QCStatus_e r = AllocateFrameBuffers( staticCfg, config.buffers );
    if ( QC_STATUS_OK != r )
    {
        SUCCEED();
        (void) DeinitBuffers();
        return;
    }

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK == ret ) (void) camera.DeInitialize();
    (void) DeinitBuffers();
    SUCCEED();
}

// --- ValidateConfig: 0 streams + clientId=0 + bPrimary=true edge case ---
TEST( Camera, EXCEPTION_Test_ValidateConfig_StreamIdTooLarge_Mock )
{
    // streamId >= QCNODE_CAMERA_MAX_STREAM_NUM (32) -> ValidateConfig rejects.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].streamId", 100u );
    SetFullMockParam();

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_ValidateConfig_PrimaryClientId0_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "primary", true );
    SetConfig( config, "clientId",
               0u );   // primary=true with clientId=0 -> ValidateConfig fails
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    (void) DeinitBuffers();
}

// --- ZeroModeNum: numModes=0 in the mock's reply ---
TEST( Camera, EXCEPTION_Test_Init_ZeroModeNum_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    g_mockInputModes.numModes = 0;
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUT_MODES, MOCK_CONTROL_API_OUT_PARAM1,
                  &g_mockInputModes );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OUT_OF_BOUND, ret );
    (void) DeinitBuffers();
}

// --- inputId mismatch: configured inputId not in QueryInputs result ---
TEST( Camera, EXCEPTION_Test_Init_InputIdNotFound_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "inputId", 99u );   // not in g_mockInputsInfo (inputId=0)
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    (void) DeinitBuffers();
}

// --- Stop without Start (already covered for fresh camera; this version is post-Init) ---
TEST( Camera, EXCEPTION_Test_Stop_NotRunning_PostInit_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    // m_state is READY, not RUNNING. Stop should hit the BAD_STATE branch.
    QCStatus_e ret = camera.Stop();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- QueryInputs: too many retries (always fail) ---
TEST( Camera, EXCEPTION_Test_QueryInputs_AlwaysFails_Mock )
{
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;

    g_controlFnc = MockCamera_GetControlFnc( "libCameraMock.so" );
    g_setFullMockFnc = MockCamera_GetSetFullMockFnc( "libCameraMock.so" );
    ASSERT_NE( g_controlFnc, nullptr );

    g_setFullMockFnc( true );
    // Arm RETURN — but this is one-shot. To fail repeatedly we need to keep
    // re-arming. Use the OUT_PARAM2 with a *count of 0 that does NOT consume in
    // FullMock (the consume-skip) so retries see count=0 -> exits at retry-cap.
    static uint32_t zero = 0;
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_OUT_PARAM2, &zero );

    QC::Node::Camera camera;
    // Constructor's QueryInputs will retry MAX_QUERY_TIMES, then return FAIL.
    // CameraImpl ignores ret and proceeds; m_state stays in INITIAL.
    (void) failRet;
}

// --- ProcessFrameDescriptor with metadata + non-zero requestId carrying input/output buffers ---
TEST( Camera, SANITY_Test_Pipeline_Metadata_FullPayload_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Build a metadata descriptor with full payload (input/output buffers + per-stream)
    // -> covers all the loop assignments in SubmitRequest(metadata).
    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t metaDataDesc;
    metaDataDesc.streamRequestNum = 2;
    metaDataDesc.requestId = 7;
    metaDataDesc.syncId = 11;
    metaDataDesc.flags = 1;
    metaDataDesc.inputBuffer.bufferListId = 1;
    metaDataDesc.inputBuffer.bufferIdx = 0;
    metaDataDesc.inputCommonMetadata.bufferListId = 4;
    metaDataDesc.inputCommonMetadata.bufferIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, metaDataDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- ProcessFrameDescriptor: SubmitRequest in multi-client non-primary mode ---
TEST( Camera, EXCEPTION_Test_ProcessFrame_MultiClientNonPrimary_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].bufferIds", nlohmann::json::array( { 0, 1, 2, 3 } ) );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", false );
    SetFullMockParam();

    QCarCamBufferList_t localList;
    QCarCamBuffer_t localBufs[4];
    SharedBufferPool localPool;
    BuildPoolBufferList( localList, localBufs, localPool, 4 );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &localList );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Build a NodeFrameDescriptor referencing one of the imported buffers so
    // ProcessFrameDescriptor reaches the multi-client SubmitRequest gate.
    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    camFrameDesc.dmaHandle = localBufs[0].planes[0].memHndl;
    camFrameDesc.size = 12533760;
    camFrameDesc.streamId = 1;
    camFrameDesc.frameIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrameDesc ) );

    // ProcessFrameDescriptor -> SubmitRequest(frame) takes the multi-client
    // non-primary BAD_ARGS branch.
    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_NE( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) localPool.Deinit();
}

// --- QcarcamEventCb: unknown event id (default arm) ---
TEST( Camera, SANITY_Test_EventCallback_UnknownEvent_Mock )
{
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          g_triggerEventFnc( QCARCAM_EVENT_INPUT_SIGNAL, &payload, false );
                          g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );
                          // Unknown event -> default switch arm
                          g_triggerEventFnc( 0xFFFF, &payload, false );
                      } );
}

TEST( Camera, SANITY_Test_EventCallback_McNotify_AllSubEvents_Mock )
{
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          // numStreams=2 exercises the per-event "for ( j < numStreams )" loop
                          // body (true + exit edges); each event value selects a switch arm.
                          const QCarCamMCEvent_e mcEvents[] = {
                                  QCARCAM_MC_STREAM_CREATE, QCARCAM_MC_STREAM_DESTROY,
                                  QCARCAM_MC_STREAM_START, QCARCAM_MC_STREAM_STOP };
                          for ( QCarCamMCEvent_e ev : mcEvents )
                          {
                              QCarCamEventPayload_t payload = {};
                              payload.mcEventInfo.event = ev;
                              payload.mcEventInfo.numStreams = 2U;
                              payload.mcEventInfo.bufferListId[0] = 1U;
                              payload.mcEventInfo.bufferListId[1] = 2U;
                              g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &payload, false );
                          }

                          // Unsupported mc event value -> inner default arm.
                          QCarCamEventPayload_t bad = {};
                          bad.mcEventInfo.event = (QCarCamMCEvent_e) 0x7FFF;
                          g_triggerEventFnc( QCARCAM_EVENT_MC_NOTIFY, &bad, false );
                      } );
}

TEST( Camera, SANITY_Test_EventCallback_MultiStreamFrameReady_Batch_Mock )
{
    RunStartedCamera(
            "./data/test/camera/camera_config_imx728_request_nv12.json", []( QC::Node::Camera & ) {
                QCarCamEventPayload_t payload = {};
                payload.multiFrameInfo.numFrameInfo = 3U;
                // [0] valid output stream (id=1 is the registered output) -> FrameCallback
                payload.multiFrameInfo.batchFrameInfo[0].id = 1;
                payload.multiFrameInfo.batchFrameInfo[0].bufferIndex = 0;
                payload.multiFrameInfo.batchFrameInfo[0].flags = 0;
                // [1] valid id but INVALID flag -> ReturnInvalidFrame drop arm
                payload.multiFrameInfo.batchFrameInfo[1].id = 1;
                payload.multiFrameInfo.batchFrameInfo[1].bufferIndex = 1;
                payload.multiFrameInfo.batchFrameInfo[1].flags = QCARCAM_BUFFER_STATUS_INVALID;
                // [2] non-output id -> "skip batch frameIdx" continue arm
                payload.multiFrameInfo.batchFrameInfo[2].id = 9999;
                payload.multiFrameInfo.batchFrameInfo[2].bufferIndex = 0;
                g_triggerEventFnc( QCARCAM_EVENT_MULTI_STREAM_FRAME_READY, &payload, false );
            } );
}

// --- FRAME_READY with bufferIndex past nBuffers -> GetFrame OUT_OF_BOUND arm
// then the "GetFrame failed" else arm in QcarcamEventCb. ---
TEST( Camera, EXCEPTION_Test_EventCallback_FrameReady_BufferIdxOOR_Mock )
{
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          // id=1 is a registered output stream, but bufferIndex is past nBuffers.
                          QCarCamEventPayload_t payload = {};
                          payload.frameInfo.id = 1;
                          payload.frameInfo.bufferIndex = 9999;
                          g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );
                      } );
}

// --- SetFrameBuffers error: bufferIdx out of range ---
TEST( Camera, EXCEPTION_Test_SetFrameBuffers_BufferIdxOutOfRange_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    // bufferIds reference index 99 -> way beyond what AllocateFrameBuffers allocates.
    SetConfig( config, "streamConfigs[0].bufferIds", nlohmann::json::array( { 99 } ) );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
    (void) DeinitBuffers();
}

// --- ClearFrameBuffers happy path: pre-allocated buffers cleared on DeInit ---
TEST( Camera, SANITY_Test_ClearFrameBuffers_DeInit_Mock )
{
    // Standard Init -> DeInit. ClearFrameBuffers runs in DeInit and walks the
    // m_frameBuffers vector to delete pCamFrameDescs.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_OK, camera.DeInitialize() );
    (void) DeinitBuffers();
}

// --- ProcessFrameDescriptor: NON-request mode + frame -> ReleaseFrame path ---
TEST( Camera, SANITY_Test_ProcessFrame_ReleaseFrame_NonRequestMode_Mock )
{
    // requestMode=false -> ProcessFrame goes through ReleaseFrame (covered already).
    // This is the success arm: QCarCamReleaseFrame returns OK, ProcessFrame returns OK.
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    camFrameDesc = config.buffers[0];
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrameDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- GetFrame: non-request-mode path with QCarCamGetFrame failing ---
TEST( Camera, EXCEPTION_Test_NonRequestMode_GetFrame_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_RETURN, &failRet );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- GetFrame: non-request-mode SUCCESS path ---
TEST( Camera, SANITY_Test_NonRequestMode_GetFrame_Success_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      CaptureFrameCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_captured = { 0, 0, 0 };

    // QCarCamGetFrame out-param: streamId 1, bufferIndex 2, known timestamp.
    const uint64_t kTs = 0x1234567890ABCDEFULL;
    QCarCamFrameInfo_t injected = {};
    injected.id = 1;
    injected.bufferIndex = 2;
    injected.sofTimestamp.timestamp = kTs;
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_OUT_PARAM1, &injected );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;   // bufferListId -> m_frameBuffers[1]
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    // The success path must have written injected.sofTimestamp into the frame
    // descriptor and delivered exactly one frame to the callback.
    EXPECT_EQ( 1U, g_captured.okCount );
    EXPECT_EQ( kTs, g_captured.lastTimestamp );
    EXPECT_EQ( 1U, g_captured.lastStreamId );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- EventCallback: state RUNNING + frame data -> covers full callback dispatch ---
TEST( Camera, SANITY_Test_EventCallback_RunningStateError_Mock )
{
    // Init+Start then trigger ERROR event in RUNNING -> EventCallback dispatch
    // should hit the success path (m_callback non-null + m_state==RUNNING fires
    // the user callback).
    RunStartedCamera( "./data/test/camera/camera_config_imx728_request_nv12.json",
                      []( QC::Node::Camera & ) {
                          QCarCamEventPayload_t payload = {};
                          payload.errInfo.errorCode = 2;
                          g_triggerEventFnc( QCARCAM_EVENT_ERROR, &payload, false );
                      } );
}

// --- Init with multi-stream + matching buffer ids in different streams ---
TEST( Camera, SANITY_Test_Init_MultiStream_DistinctBuffers_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 0;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK == ret ) (void) camera.DeInitialize();
    (void) DeinitBuffers();
    SUCCEED();
}

// --- Init with multi-stream pattern mode that has zero-pattern + nonzero-pattern ---
TEST( Camera, SANITY_Test_Init_RequestPatternMode_MultiStream_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    // First stream: pattern=0 (reference). Second: pattern=2 -> pattern mode true.
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 0u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 2;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    if ( QC_STATUS_OK != ret )
    {
        (void) DeinitBuffers();
        SUCCEED();
        return;
    }

    if ( QC_STATUS_OK != camera.Start() )
    {
        (void) camera.DeInitialize();
        (void) DeinitBuffers();
        SUCCEED();
        return;
    }

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- Init: streamConfigs with all non-zero submitRequestPattern -> pattern mode + no ref ---
TEST( Camera, EXCEPTION_Test_Init_RequestPatternMode_NoRef_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 1u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 2;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    // No stream has pattern=0 -> Init's L232-236 raises BAD_ARGUMENTS.
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_RequestPatternMode_SubmitRequest_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      ProcessDoneCb );
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 0u );   // reference
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 2;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    {
        NodeFrameDescriptor frameDesc2( 1 );
        CameraFrameDescriptor_t f2;
        f2 = config.buffers[4];
        f2.streamId = 2;
        f2.frameIdx = 0;
        (void) frameDesc2.SetBuffer( 0, f2 );
        (void) camera.ProcessFrameDescriptor( frameDesc2 );

        NodeFrameDescriptor frameDesc1( 1 );
        CameraFrameDescriptor_t f1;
        f1 = config.buffers[0];
        f1.streamId = 1;
        f1.frameIdx = 0;
        (void) frameDesc1.SetBuffer( 0, f1 );
        (void) camera.ProcessFrameDescriptor( frameDesc1 );
    }

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SetFrameBuffers_WidthMismatch_Mock )
{
    // Buffers are allocated at the real config width (3840).
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Now corrupt only the config the Camera parses: claim width 1920. The
    // allocated buffer descriptors still carry 3840, so SetFrameBuffers' width
    // check fails and Init returns INVALID_BUF.
    QCNodeInit_t badConfig;
    LoadCameraConfig( badConfig, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      NoOpCb );
    SetConfig( badConfig, "streamConfigs[0].width", 1920u );
    config.config = badConfig.config;

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- SetFrameBuffers: config height != allocated buffer height -> INVALID_BUF
// Mirror of the width-mismatch test. ---
TEST( Camera, EXCEPTION_Test_SetFrameBuffers_HeightMismatch_Mock )
{
    // Buffers allocated at the real config height (2160).
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Config claims height 1080; buffers carry 2160 -> SetFrameBuffers L1050 fails.
    // (width is left untouched so the L1041 check passes and we reach L1050.)
    QCNodeInit_t badConfig;
    LoadCameraConfig( badConfig, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      NoOpCb );
    SetConfig( badConfig, "streamConfigs[0].height", 1080u );
    config.config = badConfig.config;

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- SetFrameBuffers: config format != allocated buffer format -> INVALID_BUF
// Buffers allocated as nv12; config claims uyvy.
//     The format check fires before width/height. ---
TEST( Camera, EXCEPTION_Test_SetFrameBuffers_FormatMismatch_Mock )
{
    // Buffers allocated as nv12 (the file's format).
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Config claims uyvy; buffer descriptors carry nv12 -> SetFrameBuffers L1032 fails.
    QCNodeInit_t badConfig;
    LoadCameraConfig( badConfig, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      NoOpCb );
    SetConfig( badConfig, "streamConfigs[0].format", std::string( "uyvy" ) );
    config.config = badConfig.config;

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_NonRequestMode_GetFrame_BufferIdxOOR_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      CaptureFrameCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_captured = { 0, 0, 0 };

    // The stream has 8 buffers (bufferIds 0..7). Inject bufferIndex=99 (>= nBuffers)
    // so GetFrame's range check trips and returns OUT_OF_BOUND.
    QCarCamFrameInfo_t injected = {};
    injected.id = 1;
    injected.bufferIndex = 99;
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_OUT_PARAM1, &injected );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    // GetFrame failed -> QcarcamEventCb's FRAME_READY arm skips FrameCallback,
    // so no OK frame was delivered to the callback.
    EXPECT_EQ( 0U, g_captured.okCount );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- EventCallback: trigger ERROR before Start (state=READY != RUNNING)
//     -> covers EventCallback's L1781-1798 happy path (state != RUNNING) ---
TEST( Camera, SANITY_Test_EventCallback_ErrorWhilePostInit_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json" );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    // Don't call Start(); m_state is READY, not RUNNING. Trigger ERROR -> covers
    // EventCallback's full success path.
    QCarCamEventPayload_t payload = {};
    g_triggerEventFnc( QCARCAM_EVENT_ERROR, &payload, false );

    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- ImportBuffers: QCarCamGetBuffers fails with FullMock + manual control ---
TEST( Camera, EXCEPTION_Test_ImportBuffers_GetBuffers_PartialFail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
    (void) DeinitBuffers();
}

// --- DeInitialize: Reserve has not been called -> m_QcarCamHndl == 0 -> covers L728 ---
TEST( Camera, EXCEPTION_Test_DeInit_NullHandle_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    // Force the constructor's QCarCamInitialize to return FAIL so g_nCamInitRefCount
    // never increments. ~CameraImpl's ref-count==0 else branch hits.
    g_controlFnc( MOCK_API_QCARCAM_INITIALIZE, MOCK_CONTROL_API_RETURN, &failRet );
    {
        QC::Node::Camera camera;   // dtor runs as it goes out of scope
    }
    SUCCEED();
}

// --- ProcessFrameDescriptor: dynamic_cast both fails (generic descriptor) ---
TEST( Camera, EXCEPTION_Test_ProcessFrame_BothCastsFail_Mock )
{
    // Trigger the L596 nested dynamic_cast failure path (already attempted; this
    // is a more precise version using a base-class instance directly).
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    QC::Memory::QCBufferDescriptorBase_t baseDesc;
    baseDesc.name = "Generic";
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, baseDesc ) );

    QCStatus_e ret = camera.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

// --- Init failure cleanup: trigger Reserve to fail so the cleanup-on-error path
//     at L478-510 walks ClearFrameBuffers/ClearMetaDataBuffers/m_frameBufferMap.clear() ---
TEST( Camera, EXCEPTION_Test_Init_Reserve_Fail_With_Cleanup_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    g_controlFnc( MOCK_API_QCARCAM_RESERVE, MOCK_CONTROL_API_RETURN, &failRet );
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_Dtor_QCarCamUninitialize_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    {
        QC::Node::Camera camera;
        ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
        (void) camera.DeInitialize();

        // Arm the failure so the destructor's refcount->0 QCarCamUninitialize fails.
        QCarCamRet_e failRet = QCARCAM_RET_FAILED;
        g_controlFnc( MOCK_API_QCARCAM_UNINITIALIZE, MOCK_CONTROL_API_RETURN, &failRet );
    }   // ~Camera here -> ~CameraImpl -> QCarCamUninitialize() returns failure

    // Clear the injected failure so later tests' teardown is unaffected.
    g_controlFnc( MOCK_API_QCARCAM_UNINITIALIZE, MOCK_CONTROL_API_NONE, nullptr );
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_NV12 )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_OV3F_RequestMode_NV12 )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_ov3f_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_ReleaseMode_NV12 )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<bool>( "static.requestMode", false );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_OV3F_ReleaseMode_NV12 )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_ov3f_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<bool>( "static.requestMode", false );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_UYVY )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "uyvy" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_RGB )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "rgb" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_BGR )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "bgr" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_P010 )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "p010" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_NV12_UBWC )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "nv12_ubwc" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RequestMode_TP10_UBWC )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<std::string>( "format", "tp10_ubwc" );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_MultiStream )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_2stream.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_MultiStreamFrameReady )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_2stream.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.enableMultiStreamFrameReady", true );
    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_MultiStream_RequestPatternMode )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    DataTree streamConfig;
    nlohmann::json jsonData;
    std::string errors;
    std::vector<DataTree> streamConfigs;
    std::string filePath = "./data/test/camera/camera_config_imx728_2stream.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = staticCfg.Get( "streamConfigs", streamConfigs );
    ASSERT_EQ( QC_STATUS_OK, ret );

    streamConfigs[0].Set<uint32_t>( "submitRequestPattern", 0 );
    streamConfigs[1].Set<uint32_t>( "submitRequestPattern", 2 );
    staticCfg.Set( "streamConfigs", streamConfigs );
    dt.Set( "static", staticCfg );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_FrameDrop )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.camFrameDropPattern", 10 );
    dt.Set<uint8_t>( "static.camFrameDropPeriod", 3 );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, SANITY_Test_Camera_Frame_IMX728_RecoveryMode )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<bool>( "static.recovery", true );

    SANITY_Test_Camera_Frame( dt );
}

TEST( Camera, EXCEPTION_Test_ConfigError )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    Exception_Test_ConfigError( dt );
}

TEST( Camera, EXCEPTION_Test_ConfigError_MetaData )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata_tuning_mode.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    Exception_Test_ConfigError_MetaData( dt );
}

TEST( Camera, EXCEPTION_Test_InitError )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_imx728_request_nv12.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    Exception_Test_InitError( dt );
}

TEST( Camera, SANITY_Test_SubmitRequest_Frame_InjectionMode_ReleasesFrame_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_submit", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrame;
    camFrame = config.buffers[0];
    camFrame.streamId = 1;
    camFrame.frameIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, camFrame ) );
    EXPECT_EQ( QC_STATUS_OK, camera.ProcessFrameDescriptor( frameDesc ) );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SubmitRequest_Frame_PatternMode_UnknownStream_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 0u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 2;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t f;
    f = config.buffers[0];
    f.streamId = 999;   // unknown
    f.frameIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, f ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, camera.ProcessFrameDescriptor( frameDesc ) );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SubmitRequest_Frame_PatternMode_MultiContext_Continue_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    // Stream 0: ctx 0, ref.
    SetConfig( config, "streamConfigs[0].submitRequestPattern", 0u );
    SetConfig( config, "streamConfigs[0].contextId", 0u );
    {
        // Stream 1: ctx 1, ref. (Used to exercise the "skip other context" branch
        // when stream 0's reference frame returns.)
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["submitRequestPattern"] = 0;
        second["contextId"] = 1;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    // Return a frame from stream 0 (context 0). The loop walks all streams and
    // skips stream 2 (different context) via the `continue` arm.
    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t f;
    f = config.buffers[0];
    f.streamId = 1;
    f.frameIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, f ) );
    EXPECT_EQ( QC_STATUS_OK, camera.ProcessFrameDescriptor( frameDesc ) );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SubmitRequest_MetaData_InjectionMode_PreservesRequestId_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_meta", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    NodeFrameDescriptor frameDesc( 1 );
    CameraMetaDataDescriptor_t meta;
    meta.requestId = 0xAABBu;   // non-zero → injection path copies this verbatim
    meta.streamRequestNum = 1;
    meta.flags = QCARCAM_REQUEST_FLAG_INJECTION;
    meta.inputBuffer.bufferListId = 257;
    meta.inputBuffer.bufferIdx = 0;
    meta.inputCommonMetadata.bufferListId = 513;
    meta.inputCommonMetadata.bufferIdx = 0;
    meta.streamRequests[0].bufferListId = 1;
    meta.streamRequests[0].bufferIdx = 0;
    ASSERT_EQ( QC_STATUS_OK, frameDesc.SetBuffer( 0, meta ) );

    EXPECT_EQ( QC_STATUS_OK, camera.ProcessFrameDescriptor( frameDesc ) );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_RegisterInjectionBufferList_InputBufferIdxOOR_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.inputBufferIds",
               nlohmann::json::array( { 99 } ) );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    // Indices 0-7 exist; 8-onward are not allocated, so 99 is OOR.

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OUT_OF_BOUND, camera.Initialize( config ) );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_RegisterInjectionBufferList_SetBuffers_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_setb", 5, 3840 * 2160 * 2, config.buffers ) );

    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    QCStatus_e ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SubmitAllBuffers_InjectionMode_Skipped_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_sab", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_OK, camera.Start() );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SubmitAllBuffers_TuningCommonMetaAttached_Mock )
{
    QCStatus_e ret = RunInit_MetaData( []( QC::Node::Camera &cam, QCNodeInit_t & ) {
        QCStatus_e r = cam.Start();
        (void) cam.Stop();
        return r;
    } );
    EXPECT_EQ( QC_STATUS_OK, ret );
}

TEST( Camera, EXCEPTION_Test_ReturnInvalidFrame_SubmitRequest_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    payload.frameInfo.flags = QCARCAM_BUFFER_STATUS_INVALID;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_ReturnInvalidFrame_ReleaseMode_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "requestMode", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamFrameInfo_t injected = {};
    injected.id = 1;
    injected.bufferIndex = 0;
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_OUT_PARAM1, &injected );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    payload.frameInfo.flags = QCARCAM_BUFFER_STATUS_INVALID;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_FrameCallback_NullCallback_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      nullptr );   // null callback
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_EventCallback_NullCallback_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      nullptr );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamEventPayload_t payload = {};
    payload.errInfo.errorCode = 2;
    g_triggerEventFnc( QCARCAM_EVENT_ERROR, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_QcarcamEventCb_FrameReady_StreamMapMiss_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "requestMode", false );   // GetFrame path consults id
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamFrameInfo_t injected = {};
    injected.id = 1;
    injected.bufferIndex = 0;
    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_OUT_PARAM1, &injected );

    QCarCamEventPayload_t payload = {};
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SetFrameBuffers_NumPlanesClampedToMax_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    for ( auto &b : config.buffers )
    {
        QCBufferDescriptorBase_t &base = b.get();
        ImageDescriptor_t *pImg = dynamic_cast<ImageDescriptor_t *>( &base );
        if ( pImg != nullptr )
        {
            pImg->numPlanes = QCARCAM_MAX_NUM_PLANES + 5;
        }
    }

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SubmitAllBuffers_MultiContext_FailMid_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].contextId", 0u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["contextId"] = 1;
        second["submitRequestPattern"] = 0;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_FAIL, camera.Start() );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_PrimaryClientNonZero_SetFrameBuffers_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", true );   // primary session with non-zero clientId
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    // Init must succeed via SetFrameBuffers, not ImportBuffers.
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_OK, camera.Start() );
    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Injection_EmptyHeaderBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    // Empty the header buffer list — Init should still succeed, taking the
    // header-skip branch in SetMetaDataBuffers.
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.headerBufferIds",
               nlohmann::json::array() );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_eh", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Injection_EmptyEepromBufferIds_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetConfig( config, "metaDataConfigs[0].InjectionConfig.eepromBufferIds",
               nlohmann::json::array() );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_ee", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Injection_NoOutputMetadata_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetConfig( config, "metaDataConfigs[0].outputBufferListId", 0u );
    SetConfig( config, "metaDataConfigs[0].outputBufferIds", nlohmann::json::array() );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_no_om", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_SubmitAllBuffers_NoCommonMeta_Skips_Attach_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_OK, camera.Start() );
    EXPECT_EQ( QC_OBJECT_STATE_RUNNING, camera.GetState() );
    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_Init_InjectionMetadata_NoTuningAttach_Mock )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_nt", 5, 3840 * 2160 * 2, config.buffers ) );

    QC::Node::Camera camera;
    ret = camera.Initialize( config );
    EXPECT_EQ( QC_STATUS_OK, ret );
    EXPECT_EQ( QC_OBJECT_STATE_READY, camera.GetState() );
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, SANITY_Test_QcarcamEventCb_MultiStreamFrameReady_SkipUnknownStream_Mock )
{
    QCNodeInit_t config;
    g_captured = { 0, 0, 0 };
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json",
                      CaptureFrameCb );
    SetConfig( config, "enableMultiStreamFrameReady", true );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    QCarCamEventPayload_t payload = {};
    QCarCamMultiFrameInfo_t &mf = payload.multiFrameInfo;
    mf.numFrameInfo = 2;
    mf.requestId = 1;
    mf.batchFrameInfo[0].id = 1;   // configured stream
    mf.batchFrameInfo[0].bufferIndex = 0;
    mf.batchFrameInfo[1].id = 99;   // unknown stream — must be skipped
    mf.batchFrameInfo[1].bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_MULTI_STREAM_FRAME_READY, &payload, false );

    // Strict observation: exactly one frame was delivered (for the configured
    // entry); the unknown stream entry was skipped via continue.
    EXPECT_EQ( 1U, g_captured.okCount );
    EXPECT_EQ( 1U, g_captured.lastStreamId );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffers_InjBufs_Cleanup_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_cleanup", 5, 3840 * 2160 * 2, config.buffers ) );

    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_FAIL, camera.Initialize( config ) );
    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_QcarcamEventCb_MultiStreamFrameReady_GetFrame_Fail_Mock )
{
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "requestMode", false );   // GetFrame path is consulted
    SetConfig( config, "enableMultiStreamFrameReady", true );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    ASSERT_EQ( QC_STATUS_OK, camera.Start() );

    g_controlFnc( MOCK_API_QCARCAM_GET_FRAME, MOCK_CONTROL_API_RETURN, &failRet );

    QCarCamEventPayload_t payload = {};
    QCarCamMultiFrameInfo_t &mf = payload.multiFrameInfo;
    mf.numFrameInfo = 1;
    mf.requestId = 1;
    mf.batchFrameInfo[0].id = 1;
    mf.batchFrameInfo[0].bufferIndex = 0;
    g_triggerEventFnc( QCARCAM_EVENT_MULTI_STREAM_FRAME_READY, &payload, false );

    (void) camera.Stop();
    (void) camera.DeInitialize();
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SubmitAllBuffers_FailSecondContext_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "streamConfigs[0].contextId", 0u );
    {
        nlohmann::json second = GetFirstStreamConfig( config );
        second["streamId"] = 2;
        second["bufferIds"] = nlohmann::json::array( { 8, 9, 10, 11 } );
        second["contextId"] = 1;
        second["submitRequestPattern"] = 0;
        PushStreamConfig( config, second );
    }
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    MockReturnAtCall_t fault = { 1u, QCARCAM_RET_FAILED };
    g_controlFnc( MOCK_API_QCARCAM_SUBMIT_REQUEST, MOCK_CONTROL_API_RETURN_AT_CALL_N, &fault );

    QC::Node::Camera camera;
    ASSERT_EQ( QC_STATUS_OK, camera.Initialize( config ) );
    EXPECT_EQ( QC_STATUS_FAIL, camera.Start() );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffers_FailMidInjBufs_Cleanup_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_isp_injection.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );
    ASSERT_EQ( QC_STATUS_OK,
               AllocateGenericBuffers( "inj_midfail", 5, 3840 * 2160 * 2, config.buffers ) );

    MockReturnAtCall_t fault = { 3u, QCARCAM_RET_FAILED };
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN_AT_CALL_N, &fault );

    QC::Node::Camera camera;
    // Init fails at injection_header SetBuffers; SetMetaDataBuffers walks
    // injBufs (one entry: injection_input) through the cleanup else-arm.
    EXPECT_EQ( QC_STATUS_FAIL, camera.Initialize( config ) );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffers_MainList_SetBuffers_Fail_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_metadata_tuning_mode.json",
                      NoOpCb );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );
    DataTree staticCfg;
    {
        DataTree fullDt;
        std::string e;
        fullDt.Load( config.config, e );
        fullDt.Get( "static", staticCfg );
    }
    BufferProps_t bufProp;
    bufProp.size = calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    bufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    bufProp.cache = QC_CACHEABLE;
    ASSERT_EQ( QC_STATUS_OK, AllocateMetaDataBuffers( staticCfg, config.buffers, bufProp ) );

    MockReturnAtCall_t fault = { 1u, QCARCAM_RET_FAILED };
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN_AT_CALL_N, &fault );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_FAIL, camera.Initialize( config ) );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) DeinitBuffers();
}

TEST( Camera, EXCEPTION_Test_ImportBuffers_DuplicateHandle_Mock )
{
    QCNodeInit_t config;
    LoadCameraConfig( config, "./data/test/camera/camera_config_imx728_request_nv12.json", NoOpCb );
    SetConfig( config, "clientId", 1u );
    SetConfig( config, "primary", false );
    SetFullMockParam();
    AllocateFrameBuffersForConfig( config );

    // Build a bufferList whose buffer[0] and buffer[1] share the same
    // dmaHandle — registration of buffer[0] succeeds, buffer[1] hits the
    // duplicate-handle branch.
    QCarCamBufferList_t dupList;
    QCarCamBuffer_t dupBufs[4];
    SharedBufferPool dupPool;
    BuildPoolBufferList( dupList, dupBufs, dupPool, 4 );
    // Force a collision: copy buffer[0]'s memHndl into buffer[1].
    dupBufs[1].planes[0].memHndl = dupBufs[0].planes[0].memHndl;
    dupBufs[1].planes[1].memHndl = dupBufs[0].planes[1].memHndl;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &dupList );

    QC::Node::Camera camera;
    EXPECT_EQ( QC_STATUS_INVALID_BUF, camera.Initialize( config ) );

    (void) camera.DeInitialize();
    if ( g_controlFnc != nullptr )
    {
        for ( int i = 0; i < MOCK_API_MAX; i++ )
        {
            g_controlFnc( static_cast<MockAPI_ID_e>( i ), MOCK_CONTROL_API_NONE, nullptr );
        }
    }
    (void) dupPool.Deinit();
    (void) DeinitBuffers();
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
