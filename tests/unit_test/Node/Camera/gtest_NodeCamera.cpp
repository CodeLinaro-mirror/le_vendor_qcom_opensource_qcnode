// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "CameraMock.hpp"
#include "QC/Node/Camera.hpp"
#include "QC/sample/SharedBufferPool.hpp"
#include "gtest/gtest.h"

#include "camera_metadata.h"
#include "qcarcam_metadata.h"

using namespace QC;
using namespace QC::Node;
using namespace QC::sample;

QC::Node::Camera *g_pCamera = nullptr;
uint32_t g_frameIdx = 0;
std::vector<SharedBufferPool> g_bufferPools;

// global mock param
QCarCamInput_t g_mockInputsInfo;
QCarCamMode_t g_mockCamMode;
QCarCamInputModes_t g_mockInputModes;
MockApi_ControlFnc_t g_controlFnc = nullptr;
MockApi_SetErrorPassiveFnc_t g_setErrorPassiveFnc = nullptr;
MockApi_TriggerEventFnc_t g_triggerEventFnc = nullptr;

// global metadata param
const uint32_t MAX_METADATA_TAG_NUM = 50;
const uint32_t MAX_METADATA_TAG_DATA = 65536;
BufferProps_t g_bufferProp;
QCarCamBufferList_t g_bufferList;
uint32_t g_metaDatabufferNum = 4;
uint32_t g_metaDataPlaneNum = 2;
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
    QCStatus_e ret = QC_STATUS_OK;

    QCFrameDescriptorNodeIfs &frameDescIfs = eventInfo.frameDesc;
    QCBufferDescriptorBase_t &bufDesc = frameDescIfs.GetBuffer( 0 );
    NodeFrameDescriptor frameDesc( 1 );

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
    g_bufferPools.resize( metaDataNum );

    for ( uint32_t i = 0; i < metaDataNum; i++ )
    {
        std::string bufPoolName = name + "_metadata_" + std::to_string( i );
        metaDataConfig = metaDataConfigs[i];
        bufferListId = metaDataConfig.Get<uint32_t>( "bufferListId", UINT32_MAX );
        std::vector<uint32_t> bufferIds =
                metaDataConfig.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        bufferNum = bufferIds.size();

        ret = g_bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                     bufferProp );
        if ( QC_STATUS_OK != ret )
        {
            break;
        }

        ret = g_bufferPools[i].GetBuffers( buffers );
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

    return ret;
}

void SetGlobalMockParam()
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
    ASSERT_NE( g_controlFnc, nullptr );

    g_setErrorPassiveFnc = MockCamera_GetSetErrorPassiveFnc( "libCameraMock.so" );
    ASSERT_NE( g_setErrorPassiveFnc, nullptr );

    g_triggerEventFnc = MockCamera_GetTriggerEventFnc( "libCameraMock.so" );
    ASSERT_NE( g_triggerEventFnc, nullptr );

    g_setErrorPassiveFnc( true );
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_OUT_PARAM1, &g_mockInputsInfo );
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUT_MODES, MOCK_CONTROL_API_OUT_PARAM1,
                  &g_mockInputModes );
}

void SetGlobalMetaDataParam( DataTree &staticCfg, QCNodeInit_t &config )
{
    QCStatus_e ret;
    g_metaDatabufferNum = 4;
    g_metaDataPlaneNum = 2;
    g_bufferProp.size =
            calculate_camera_metadata_size( MAX_METADATA_TAG_NUM, MAX_METADATA_TAG_DATA );
    g_bufferProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
    g_bufferProp.cache = QC_CACHEABLE;

    memset( &g_bufferList, 0, sizeof( g_bufferList ) );
    g_bufferList.id = 1;
    g_bufferList.nBuffers = g_metaDatabufferNum;
    g_bufferList.pBuffers = new QCarCamBuffer_t[g_metaDatabufferNum];
    g_bufferList.colorFmt = QCARCAM_FMT_NV12;
    g_bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;
    g_pCamFrameDescs = new CameraFrameDescriptor_t[g_metaDatabufferNum];

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = AllocateMetaDataBuffers( staticCfg, config.buffers, g_bufferProp );
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( uint32_t i = 0; i < g_metaDatabufferNum; i++ )
    {
        g_pCamFrameDescs[i] = config.buffers[i];
        g_bufferList.pBuffers[i].numPlanes = g_metaDataPlaneNum;
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

    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );

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

    SetGlobalMockParam();

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
    const std::string &opts = monitorIfs.GetOptions();
    ASSERT_EQ( "{}", opts );

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
    const std::string &cfgOpts = configIfs.GetOptions();
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

    SetGlobalMockParam();

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

    SetGlobalMockParam();

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

    SetGlobalMockParam();

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

    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );

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

    SetGlobalMockParam();

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

    SetGlobalMockParam();

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

    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );

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

void Exception_Test_MultiClient( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    NodeFrameDescriptor frameDesc( 1 );
    CameraFrameDescriptor_t camFrameDesc;
    CameraFrameDescriptor_t originalCamFrameDesc;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCarCamRet_e correctRet = QCARCAM_RET_OK;

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );

    uint64_t dmaHandle = g_bufferList.pBuffers[0].planes[0].memHndl;
    g_bufferList.pBuffers[0].numPlanes = 4;
    g_bufferList.pBuffers[0].planes[0].memHndl = 0;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );
    g_pCamera = new QC::Node::Camera();

    // map memory failure
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

    // QCarCamGetBuffers for frame failure
    g_bufferList.pBuffers[0].planes[0].memHndl = dmaHandle;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );
    g_pCamera = new QC::Node::Camera();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    // QCarCamSetBuffers for metadata failure
    delete g_pCamera;
    g_pCamera = nullptr;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_RETURN, &correctRet );
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &failRet );
    g_pCamera = new QC::Node::Camera();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_FAIL, ret );

    delete g_pCamera;
    g_pCamera = nullptr;
    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );
    g_bufferList.pBuffers[1].planes[0].memHndl = g_pCamFrameDescs[0].dmaHandle;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );
    g_controlFnc( MOCK_API_QCARCAM_SET_BUFFERS, MOCK_CONTROL_API_RETURN, &correctRet );

    g_pCamera = new QC::Node::Camera();

    // use a registered buffer
    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_INVALID_BUF, ret );

    delete g_pCamera;
    g_pCamera = nullptr;
    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // initialize successfully
    SetGlobalMockParam();
    SetGlobalMetaDataParam( staticCfg, config );
    g_bufferList.pBuffers[1].planes[0].memHndl = g_pCamFrameDescs[1].dmaHandle;
    g_controlFnc( MOCK_API_QCARCAM_GET_BUFFERS, MOCK_CONTROL_API_OUT_PARAM1, &g_bufferList );
    g_pCamera = new QC::Node::Camera();

    ret = g_pCamera->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // SubmitRequest for frame failure
    camFrameDesc = config.buffers[0];
    camFrameDesc.dmaHandle = g_pCamFrameDescs[1].dmaHandle;
    ret = frameDesc.SetBuffer( 0, camFrameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    ret = g_pCamera->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = g_pCamera->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    delete g_pCamera;
    g_pCamera = nullptr;

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

    config.config = dt.Dump();
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetGlobalMockParam();

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
    payload.frameInfo.id = 1;
    payload.frameInfo.bufferIndex = 0;
    payload.frameInfo.sofTimestamp.timestamp = 1000;
    payload.frameInfo.sofTimestamp.timestampGPTP = 2000;
    payload.frameInfo.flags = 0;
    g_triggerEventFnc( QCARCAM_EVENT_FRAME_READY, &payload, false );

    usleep( 10000 );

    // QCARCAM_EVENT_MC_NOTIFY – QCARCAM_MC_STREAM_CREATE
    payload.mcEventInfo.event = QCARCAM_MC_STREAM_CREATE;
    payload.mcEventInfo.numStreams = 1;
    payload.mcEventInfo.bufferListId[0] = 0;
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

    SetGlobalMockParam();

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

    SetGlobalMockParam();

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

    // Delete camera - destructor: g_nCamInitRefCount = 0, takes else branch (line 118)
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

    SetGlobalMockParam();

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

void Exception_Test_MultipleCamera( DataTree &dt )
{
    QCStatus_e ret;
    DataTree staticCfg;
    QCNodeInit_t config;
    QCarCamRet_e failRet = QCARCAM_RET_FAILED;
    QCarCamRet_e correctRet = QCARCAM_RET_OK;

    config = { dt.Dump() };
    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    ASSERT_EQ( QC_STATUS_OK, ret );

    SetGlobalMockParam();

    ret = AllocateFrameBuffers( staticCfg, config.buffers );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // First Camera: g_nCamInitRefCount was 0 -> QCarCamInitialize called -> g_nCamInitRefCount = 1
    QC::Node::Camera *pCamera1 = new QC::Node::Camera();

    // Second Camera: g_nCamInitRefCount was 1 -> else branch (just increment refcount)
    // Set QCarCamQueryInputs failure for Camera2
    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_RETURN, &failRet );
    QC::Node::Camera *pCamera2 = new QC::Node::Camera();

    // Camera2 QCarCamQueryInputs successfully
    delete pCamera2;
    pCamera2 = nullptr;

    g_controlFnc( MOCK_API_QCARCAM_QUERY_INPUTS, MOCK_CONTROL_API_RETURN, &correctRet );
    pCamera2 = new QC::Node::Camera();

    // Initialize first camera
    ret = pCamera1->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // Initialize second camera
    ret = pCamera2->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCamera1->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCamera2->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    // Delete first camera: g_nCamInitRefCount-- (2->1), "Skip QCarCamUninitialize" path
    delete pCamera1;
    pCamera1 = nullptr;

    // Delete second camera: g_nCamInitRefCount-- (1->0), QCarCamUninitialize called
    delete pCamera2;
    pCamera2 = nullptr;

    ret = DeinitBuffers();
    ASSERT_EQ( QC_STATUS_OK, ret );
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

TEST( Camera, SANITY_Test_Camera_MetaData )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );

    SANITY_Test_Camera_MetaData( dt );
}

TEST( Camera, SANITY_Test_Camera_MultiClient )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    dt.Set<uint32_t>( "static.clientId", 1 );

    SANITY_Test_Camera_MetaData( dt );
}

TEST( Camera, SANITY_Test_CameraMonitor )
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
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

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

TEST( Camera, EXCEPTION_Test_StartStopError )
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

TEST( Camera, EXCEPTION_Test_SetFrameBuffer )
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

TEST( Camera, EXCEPTION_Test_SetMetaDataBuffer )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SetMetaDataBuffer( dt );
}

TEST( Camera, EXCEPTION_Test_SubmitRequest_Frame )
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

TEST( Camera, EXCEPTION_Test_ReleaseFrame )
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

TEST( Camera, Exception_Test_SubmitRequest_MetaData )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    Exception_Test_SubmitRequest_MetaData( dt );
}

TEST( Camera, Exception_Test_MultiClient )
{
    QCStatus_e ret;
    DataTree dt;
    nlohmann::json jsonData;
    std::string errors;
    std::string filePath = "./data/test/camera/camera_config_metadata.json";

    ReadJsonFile( filePath, jsonData );
    std::string jsonStr = jsonData.dump();

    ret = dt.Load( jsonStr, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<uint32_t>( "static.inputId", 0 );
    dt.Set<uint32_t>( "static.clientId", 1 );
    Exception_Test_MultiClient( dt );
}

TEST( Camera, EXCEPTION_Test_EventCallback )
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

TEST( Camera, EXCEPTION_Test_QueryInputs )
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

TEST( Camera, EXCEPTION_Test_ZeroModeNum )
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

TEST( Camera, EXCEPTION_Test_QCarCamInitialize )
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

TEST( Camera, EXCEPTION_Test_GetFrame_Failure )
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

TEST( Camera, Exception_Test_MultipleCamera )
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
    Exception_Test_MultipleCamera( dt );
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
