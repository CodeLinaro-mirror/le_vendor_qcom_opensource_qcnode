// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <cstring>
#include <new>
#include <stdint.h>
#include <thread>

#include "CameraImpl.hpp"

namespace QC
{
namespace Node
{

#if defined( __QNXNTO__ )
using MemUtils = QC::Memory::PMEMUtils;
#else
using MemUtils = QC::Memory::DMABUFFUtils;
#endif

static int g_nCamInitRefCount = 0;
static std::mutex g_camInitMutex;

static CameraInputs_t s_cameraInputsInfo = { nullptr, nullptr, 0 };

static void FreeCameraInputsInfo( void )
{
    if ( nullptr != s_cameraInputsInfo.pCameraInputs )
    {
        delete ( s_cameraInputsInfo.pCameraInputs );
        s_cameraInputsInfo.pCameraInputs = nullptr;
    }
    if ( nullptr != s_cameraInputsInfo.pCamInputModes )
    {
        for ( uint32_t i = 0; i < s_cameraInputsInfo.numInputs; i++ )
        {
            if ( nullptr != s_cameraInputsInfo.pCamInputModes[i].pModes )
            {
                delete ( s_cameraInputsInfo.pCamInputModes[i].pModes );
                s_cameraInputsInfo.pCamInputModes[i].pModes = nullptr;
            }
        }
        delete ( s_cameraInputsInfo.pCamInputModes );
        s_cameraInputsInfo.pCamInputModes = nullptr;
    }

    s_cameraInputsInfo.numInputs = 0;
}

CameraImpl::CameraImpl( QCNodeID_t &nodeId, Logger &logger )
    : m_nodeId( nodeId ),
      m_logger( logger ),
      m_state( QC_OBJECT_STATE_INITIAL ),
      m_bIsPrimary( false ),
      m_enableMetaData( false ),
      m_clientId( 0 ),
      m_QcarCamHndl( QCARCAM_HNDL_INVALID )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    QCarCamInit_t qcarcamInit = { 0 };
    qcarcamInit.apiVersion = QCARCAM_VERSION;

    std::lock_guard<std::mutex> guard( g_camInitMutex );
    if ( 0 == g_nCamInitRefCount )
    {
        status = QCarCamInitialize( (const QCarCamInit_t *) &qcarcamInit );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            m_state = QC_OBJECT_STATE_ERROR;
            QC_ERROR( "Failed to initialize QCarCamera", status );
        }
        else
        {
            QC_INFO( "Initialize QCarCamera successfully" );
            g_nCamInitRefCount++;

            ret = QueryInputs();
            if ( QC_STATUS_OK != ret )
            {
                ret = QC_STATUS_OK;
                QC_ERROR( "QueryInputs failed: %d", ret );
            }
        }
    }
    else
    {
        g_nCamInitRefCount++;
    }
}

CameraImpl::~CameraImpl()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    std::lock_guard<std::mutex> guard( g_camInitMutex );
    if ( 0 < g_nCamInitRefCount )
    {
        g_nCamInitRefCount--;

        if ( 0 == g_nCamInitRefCount )
        {
            status = QCarCamUninitialize();
            if ( QCARCAM_RET_OK != status )
            {
                ret = QC_STATUS_FAIL;
                QC_ERROR( "Failed to deinit QCarCamera: %d", status );
            }

            FreeCameraInputsInfo();
        }
        else
        {
            QC_INFO( "Skip QCarCamUninitialize" );
        }
    }
    else
    {
        QC_ERROR( "g_nCamInitRefCount not greater than 0, unexpected" );
    }

    for ( uint32_t i = 0; i < QCNODE_CAMERA_MAX_STREAM_NUM; i++ )
    {
        std::queue<uint32_t> empty;
        if ( false == m_freeBufIdxQueue[i].empty() )
        {
            std::swap( m_freeBufIdxQueue[i], empty );
        }
    }

    m_callback = nullptr;
}

QCStatus_e
CameraImpl::Initialize( QCNodeEventCallBack_t callback,
                        std::vector<std::reference_wrapper<QCBufferDescriptorBase>> &buffers )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    uint32_t param = 0;
    bool isQCarCamera = false;
    CameraInputs_t camInputsInfo;

    QC_INFO( "Camera node version: %u.%u.%u", QCNODE_CAMERA_VERSION_MAJOR,
             QCNODE_CAMERA_VERSION_MINOR, QCNODE_CAMERA_VERSION_PATCH );

    QC_TRACE_INIT( [&]() {
        std::ostringstream oss;
        oss << "{";
        oss << "\"name\": \"" << m_nodeId.name << "\", ";
        oss << "\"processor\": \"" << "camera" << "\"";
        oss << "}";
        return oss.str();
    }() );
    QC_TRACE_BEGIN( "Init", {} );

    if ( QC_OBJECT_STATE_INITIAL != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Camera not in initial state!" );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = ValidateConfig( m_config );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_state = QC_OBJECT_STATE_INITIALIZING;

        m_inputId = m_config.inputId;
        m_clientId = m_config.clientId;
        m_streamNum = m_config.streamConfigs.size();
        m_metaDataNum = 0;
        m_maxBufCnt = 0;
        m_bRequestMode = m_config.bRequestMode;
        m_bIsPrimary = m_config.bPrimary;
        m_enableMetaData = m_config.bEnalbleMetaData;
        m_enableInjection = false;
        m_bRecovery = m_config.bRecovery;
        m_callback = callback;

        if ( true == m_enableMetaData )
        {
            if ( false == m_bRequestMode )
            {
                ret = QC_STATUS_BAD_ARGUMENTS;
                QC_ERROR( "Metadata could only be enabled with request mode" );
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_metaDataNum = m_config.metaDataConfigs.size();
        m_streamIdToIndexMap.clear();
        for ( uint32_t i = 0; i < m_streamNum; i++ )
        {
            m_streamConfigs[i] = m_config.streamConfigs[i];
            m_streamIdToIndexMap[m_streamConfigs[i].streamId] = i;
            size_t bufferNum = m_streamConfigs[i].bufferIds.size();
            if ( bufferNum > m_maxBufCnt )
            {
                m_maxBufCnt = bufferNum;
            }
        }

        // Detect injection usecase
        if ( true == m_enableMetaData )
        {
            for ( size_t i = 0; i < m_metaDataNum; i++ )
            {
                if ( CAMERA_METADATA_TYPE_ISP_INJECTION ==
                     m_config.metaDataConfigs[i].metaDataType )
                {
                    m_enableInjection = true;
                    QC_INFO( "ISP injection usecase detected for metadata config %u", i );
                    break;
                }
            }
        }

        // Initialize current input common metadata for TUNING_FEATURE sticky modes.

        m_currentInputCommonMetadata.bufferListId = 0;
        m_currentInputCommonMetadata.bufferIdx = 0;
        if ( ( true == m_enableMetaData ) && ( false == m_enableInjection ) )
        {
            for ( size_t i = 0; i < m_metaDataNum; i++ )
            {
                const CameraMetaDataConfig_t &cfg = m_config.metaDataConfigs[i];
                uint32_t bufferListType = QCARCAM_GET_BUFFERLIST_TYPE( cfg.bufferListId );
                if ( ( QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA == bufferListType ) &&
                     ( CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE == cfg.metaDataType ) )
                {
                    m_currentInputCommonMetadata.bufferListId = cfg.bufferListId;
                    m_currentInputCommonMetadata.bufferIdx = 0;
                    QC_INFO( "Initial inputCommonMetadata: bufferListId=%u, bufferIdx=0",
                             cfg.bufferListId );
                    break;
                }
            }
        }

        /* setup submit request pattern for multiple streaming */
        m_bRequestPatternMode = false;
        m_refStreamIdByContext.clear();
        if ( ( true == m_bRequestMode ) && ( m_streamNum > 1 ) )
        {
            // First pass: copy per-stream pattern values and detect whether pattern mode is needed.
            std::unordered_set<uint32_t> ctxWithNonZeroPattern;
            for ( uint32_t i = 0; i < m_streamNum; i++ )
            {
                uint32_t ctxId = m_streamConfigs[i].contextId;
                if ( 0 == m_streamConfigs[i].submitRequestPattern )
                {
                    m_submitRequestPattern[i] = 0;
                    // First stream with pattern 0 in this context becomes its reference.
                    if ( m_refStreamIdByContext.find( ctxId ) == m_refStreamIdByContext.end() )
                    {
                        m_refStreamIdByContext[ctxId] = m_streamConfigs[i].streamId;
                    }
                }
                else
                {
                    // Non-zero pattern → FPS HW drop control; pattern mode is on.
                    m_submitRequestPattern[i] = m_streamConfigs[i].submitRequestPattern;
                    m_bRequestPatternMode = true;
                    ctxWithNonZeroPattern.insert( ctxId );
                }
            }

            if ( true == m_bRequestPatternMode )
            {
                for ( uint32_t ctxId : ctxWithNonZeroPattern )
                {
                    if ( m_refStreamIdByContext.find( ctxId ) == m_refStreamIdByContext.end() )
                    {
                        ret = QC_STATUS_BAD_ARGUMENTS;
                        QC_ERROR( "Context %u needs at least 1 stream with submitRequestPattern=0",
                                  ctxId );
                        break;
                    }
                }
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = GetInputsInfo( &camInputsInfo );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "GetInputsInfo failed: %d", ret );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        QCarCamInputModes_t *pCamInputModes = nullptr;
        for ( uint32_t i = 0; i < camInputsInfo.numInputs; i++ )
        {
            if ( camInputsInfo.pCameraInputs[i].inputId == m_inputId )
            {
                pCamInputModes = &camInputsInfo.pCamInputModes[i];
                break;
            }
        }

        if ( nullptr == pCamInputModes )
        {
            if ( m_enableInjection )
            {
                // For injection mode the virtual injection input is not returned by
                // QCarCamQueryInputs. Skip the validation and proceed to QCarCamOpen
                // directly with the configured input ID.
                QC_INFO( "camera input id %u not found in QCarCamQueryInputs results, but "
                         "injection mode is enabled — proceeding to QCarCamOpen directly "
                         "(virtual injection input is not enumerated by QCarCamQueryInputs).",
                         m_inputId );
            }
            else
            {
                ret = QC_STATUS_BAD_ARGUMENTS;
                QC_ERROR( "camera input id %u not found", m_inputId );
            }
        }
        else if ( 0 == pCamInputModes->numModes )
        {
            ret = QC_STATUS_OUT_OF_BOUND;
            QC_ERROR( "no mode 0 for camera input id %u", m_inputId );
        }
        else
        {
            // do nothing
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        QCarCamInputStream_t inputParams = { 0 };
        inputParams.inputId = m_inputId;
        inputParams.srcId = m_config.srcId;
        inputParams.inputMode = m_config.inputMode;

        QCarCamOpen_t openParams = { (QCarCamOpmode_e) 0, 0 };
        openParams.opMode = (QCarCamOpmode_e) m_config.opMode;
        openParams.numInputs = 1;
        openParams.clientId = m_clientId;
        openParams.inputs[0] = inputParams;

        if ( m_bRecovery )
        {
            openParams.flags |= QCARCAM_OPEN_FLAGS_RECOVERY;
        }

        if ( m_bRequestMode )
        {
            openParams.flags |= QCARCAM_OPEN_FLAGS_REQUEST_MODE;
        }

        // Add RECOVERY and MULTI_FRAME_INFO flags for ISP injection mode
        if ( m_enableInjection )
        {
            openParams.flags |= QCARCAM_OPEN_FLAGS_RECOVERY;
            openParams.flags |= QCARCAM_OPEN_FLAGS_MULTI_FRAME_INFO;
        }

        if ( 0 != m_clientId )
        {
            openParams.flags |= QCARCAM_OPEN_FLAGS_MULTI_CLIENT_SESSION;
        }

        status = QCarCamOpen( &openParams, &m_QcarCamHndl );
        if ( ( QCARCAM_RET_OK != status ) || ( QCARCAM_HNDL_INVALID == m_QcarCamHndl ) )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "QCarCamOpen failed: %d", status );
        }
        else
        {
            QC_INFO( "QCarCamOpen Success handle: %lu", m_QcarCamHndl );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        status = QCarCamRegisterEventCallback( m_QcarCamHndl, &QcarcamEventCb, this );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            m_state = QC_OBJECT_STATE_ERROR;
            QC_ERROR( "SetParam for QCARCAM_PARAM_EVENT_CB failed, ret=%d", status );
        }
        else
        {
            QC_INFO( "SetParam for QCARCAM_PARAM_EVENT_CB Success" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        param = QCARCAM_EVENT_INPUT_SIGNAL | QCARCAM_EVENT_ERROR | QCARCAM_EVENT_MC_NOTIFY;
        if ( true == m_config.bMultiStreamFrameReady )
        {
            param |= QCARCAM_EVENT_MULTI_STREAM_FRAME_READY;
        }
        else
        {
            param |= QCARCAM_EVENT_FRAME_READY;
        }
        status = QCarCamSetParam( m_QcarCamHndl, QCARCAM_STREAM_CONFIG_PARAM_EVENT_MASK, &param,
                                  sizeof( param ) );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "SetParam for QCARCAM_PARAM_EVENT_MASK failed, ret=%d", status );
        }
        else
        {
            QC_INFO( "SetParam for QCARCAM_PARAM_EVENT_MASK Success" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        // setup isp settings
        QCarCamIspUsecaseConfig_t ispConfig = { 0 };

        ispConfig.id = 0;
        ispConfig.cameraId = 0;
        ispConfig.usecaseId = (QCarCamIspUsecase_e) m_config.ispUseCase;

        status = QCarCamSetParam( m_QcarCamHndl, QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE,
                                  &ispConfig, sizeof( ispConfig ) );
        if ( status != QCARCAM_RET_OK )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "SetParam for QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE failed, ret=%d",
                      status );
        }
        else
        {
            QC_INFO( "SetParam for QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE success" );
        }
    }

    // For ISP injection mode, set batch mode to 1 frame per batch per output stream.
    if ( ( QC_STATUS_OK == ret ) && ( true == m_enableInjection ) )
    {
        for ( size_t i = 0; ( QC_STATUS_OK == ret ) && ( i < m_streamNum ); i++ )
        {
            QCarCamBatchConfig_t batchConfig = {};
            batchConfig.mode = QCARCAM_BATCH_MODE_FILL_BATCH_RESULT;
            batchConfig.numBatchFrames = 1;
            batchConfig.frameIncrement = 0;

            QCarCamSetParamEx_t paramEx = {};
            paramEx.param = QCARCAM_STREAM_CONFIG_PARAM_BATCH_MODE;
            paramEx.u.bufferlistId = m_streamConfigs[i].streamId;
            paramEx.pValue = &batchConfig;
            paramEx.size = sizeof( batchConfig );

            status = QCarCamSetParamEx( m_QcarCamHndl, &paramEx );
            if ( QCARCAM_RET_OK != status )
            {
                ret = QC_STATUS_FAIL;
                QC_ERROR( "SetParamEx for QCARCAM_STREAM_CONFIG_PARAM_BATCH_MODE failed, "
                          "streamId=%u, ret=%d",
                          m_streamConfigs[i].streamId, status );
            }
            else
            {
                QC_INFO( "SetParamEx for QCARCAM_STREAM_CONFIG_PARAM_BATCH_MODE success "
                         "(streamId=%u, numBatchFrames=1 for ISP injection)",
                         m_streamConfigs[i].streamId );
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( 0 == m_config.camFrameDropPattern )
        {
            QC_INFO( "Ignore frame drop config" );
        }
        else
        {
            // setup frame rate params
            QCarCamFrameDropConfig_t frameDropConfig = { 0 };
            frameDropConfig.frameDropPeriod = m_config.camFrameDropPeriod;
            frameDropConfig.frameDropPattern = m_config.camFrameDropPattern;
            status = QCarCamSetParam( m_QcarCamHndl, QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL,
                                      &frameDropConfig, sizeof( frameDropConfig ) );
            if ( QCARCAM_RET_OK != status )
            {
                QC_ERROR( "SetParam for QCARCAM_PARAM_FRAME_RATE failed, ret=%d", status );
                ret = QC_STATUS_FAIL;
            }
            else
            {
                QC_INFO( "SetParam for QCARCAM_PARAM_FRAME_RATE Success" );
            }
        }
    }

    if ( ( QC_STATUS_OK == ret ) && ( true == m_enableInjection ) )
    {
        // Setup metadata injection configs
        for ( size_t i = 0; i < m_metaDataNum; i++ )
        {
            const CameraMetaDataConfig_t &metaDataConfig = m_config.metaDataConfigs[i];
            if ( CAMERA_METADATA_TYPE_ISP_INJECTION == metaDataConfig.metaDataType )
            {
                QCarCamInjectCfg_t injectCfg = { 0 };
                injectCfg.inputId = metaDataConfig.injectionConfig.inputId;
                injectCfg.inputMode = metaDataConfig.injectionConfig.inputMode;
                injectCfg.inputTuningParamFeature1Mode =
                        metaDataConfig.injectionConfig.inputTuningParamFeature1Mode;
                injectCfg.inputTuningParamFeature2Mode =
                        metaDataConfig.injectionConfig.inputTuningParamFeature2Mode;
                injectCfg.inputSceneMode = metaDataConfig.injectionConfig.inputSceneMode;
                status = QCarCamSetParam( m_QcarCamHndl,
                                          QCARCAM_STREAM_CONFIG_PARAM_STANDALONE_INJECTION_CONFIG,
                                          &injectCfg, sizeof( injectCfg ) );

                if ( QCARCAM_RET_OK != status )
                {
                    ret = QC_STATUS_FAIL;
                    QC_ERROR( "SetParam for "
                              "QCARCAM_STREAM_CONFIG_PARAM_STANDALONE_INJECTION_CONFIG failed, "
                              "ret=%d",
                              status );
                }
                else
                {
                    QC_INFO( "SetParam for QCARCAM_STREAM_CONFIG_PARAM_STANDALONE_INJECTION_CONFIG "
                             "success" );
                }
                break;
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_frameBuffers.resize( m_streamNum );

        if ( ( 0 != m_clientId ) && ( false == m_bIsPrimary ) )
        {
            /* for multi-client, non primary session, query the primary's buffers */
            ret = ImportBuffers();
            if ( QC_STATUS_OK == ret )
            {
                QC_INFO( "Import buffers successfully" );
            }
            else
            {
                QC_ERROR( "Failed to import buffers" );
            }
        }
        else
        {
            /* set frame buffers */
            ret = SetFrameBuffers( buffers );
            if ( QC_STATUS_OK == ret )
            {
                QC_INFO( "Set frame buffers successfully" );
            }
            else
            {
                QC_ERROR( "Failed to set frame buffers" );
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        /* Set metadata buffers */
        if ( true == m_enableMetaData )
        {
            m_metaDataBuffers.resize( m_metaDataNum );
            ret = SetMetaDataBuffers( buffers );
            if ( QC_STATUS_OK == ret )
            {
                QC_INFO( "Set metadata buffers successfully" );
            }
            else
            {
                QC_ERROR( "Failed to set metadata buffers" );
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_requestId = 0;

        status = QCarCamReserve( m_QcarCamHndl );
        if ( QCARCAM_RET_OK == status )
        {
            QC_INFO( "QCarCamReserve successfully" );
        }
        else
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "QCarCamReserve failed with ret %d, exit", status );
        }
    }

    if ( ( QC_STATUS_OK == ret ) && ( true == m_enableMetaData ) )
    {
        // Configure initial ISP settings for sticky metadata features (TUNING_FEATURE modes)
        for ( size_t i = 0; i < m_metaDataNum; i++ )
        {
            const CameraMetaDataConfig_t &metaDataConfig = m_config.metaDataConfigs[i];
            uint32_t bufferListType = QCARCAM_GET_BUFFERLIST_TYPE( metaDataConfig.bufferListId );

            // Only INPUT_METADATA buffer lists carry sticky metadata values
            if ( QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA != bufferListType )
            {
                continue;
            }

            if ( CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE == metaDataConfig.metaDataType )
            {
                QCarCamIspSettings_t ispSettings = { 0 };
                ispSettings.settingsId = (uint32_t) i;
                ispSettings.inputCommonMetadata.bufferlistId = metaDataConfig.bufferListId;
                ispSettings.inputCommonMetadata.bufferIdx = 0;
                status = QCarCamSetParam( m_QcarCamHndl, QCARCAM_STREAM_CONFIG_PARAM_ISP_SETTINGS,
                                          &ispSettings, sizeof( ispSettings ) );

                if ( QCARCAM_RET_OK != status )
                {
                    QC_ERROR( "SetParam for QCARCAM_STREAM_CONFIG_PARAM_ISP_SETTINGS failed , "
                              "ret=%d, metaDataIdx=%u, bufferlistId=%u",
                              status, i, metaDataConfig.bufferListId );
                    ret = QC_STATUS_FAIL;
                    break;
                }
                else
                {
                    QC_INFO( "SetParam for QCARCAM_STREAM_CONFIG_PARAM_ISP_SETTINGS Success, "
                             "metaDataType=%d, bufferlistId=%u",
                             (int) metaDataConfig.metaDataType, metaDataConfig.bufferListId );
                }
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_state = QC_OBJECT_STATE_READY;
        QC_INFO( "Initialize camera successfully" );
    }
    else
    {
        m_state = QC_OBJECT_STATE_INITIAL;

        /* clean up QCarCamHndl */
        if ( QCARCAM_HNDL_INVALID != m_QcarCamHndl )
        {
            QC_ERROR( "Error happens in Init" );
            (void) QCarCamClose( m_QcarCamHndl );
            m_QcarCamHndl = QCARCAM_HNDL_INVALID;
        }

        // clear buffers
        if ( ( 0 != m_clientId ) && ( false == m_bIsPrimary ) )
        {
            (void) UnImportBuffers();
        }
        ClearFrameBuffers();
        ClearMetaDataBuffers();
        if ( true != m_frameBufferMap.empty() )
        {
            m_frameBufferMap.clear();
        }
        if ( true != m_metaDataBufferMap.empty() )
        {
            m_metaDataBufferMap.clear();
        }

        // clear stream configs
        if ( m_config.streamConfigs.size() > 0 )
        {
            m_config.streamConfigs.clear();
        }

        // clear metadata configs
        if ( true == m_enableMetaData )
        {
            if ( m_config.metaDataConfigs.size() > 0 )
            {
                m_config.metaDataConfigs.clear();
            }
        }
    }

    QC_TRACE_END( "Init", {} );

    return ret;
}

QCStatus_e CameraImpl::Start()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    bool bStartOK = false;

    QC_TRACE_BEGIN( "Start", {} );

    if ( QC_OBJECT_STATE_READY == m_state )
    {
        m_state = QC_OBJECT_STATE_STARTING;
        status = QCarCamStart( m_QcarCamHndl );
        if ( QCARCAM_RET_OK == status )
        {
            bStartOK = true;
            m_state = QC_OBJECT_STATE_RUNNING;
            QC_INFO( "QCarCamStart success" );
        }
        else
        {
            QC_ERROR( "QCarCamStart failed with ret %d , exit", status );
            ret = QC_STATUS_FAIL;
        }

        if ( ( QC_STATUS_OK == ret ) && ( true == m_bRequestMode ) )
        {
            ret = SubmitAllBuffers();
        }

        if ( QC_STATUS_OK != ret )
        {
            /* error clean up */
            m_state = QC_OBJECT_STATE_READY;
            if ( bStartOK )
            {
                (void) QCarCamStop( m_QcarCamHndl );
            }
        }
    }
    else
    {
        QC_ERROR( "Camera not in ready state: %d", m_state.load() );
        ret = QC_STATUS_BAD_STATE;
    }

    QC_TRACE_END( "Start", {} );

    return ret;
}

QCStatus_e CameraImpl::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e ret = QC_STATUS_OK;

    bool isFrameDesc = true;
    uint32_t streamId = 0;
    uint64_t frameId = 0;
    uint32_t bufferListId = 0;
    uint64_t bufferIdx = 0;
    uint64_t bufferHandle = 0;
    QCBufferDescriptorBase_t &bufDesc = frameDesc.GetBuffer( 0 );
    CameraFrameDescriptor_t *pCamFrameDesc = nullptr;
    CameraMetaDataDescriptor_t *pCamMetaDataDesc = nullptr;

    if ( QC_OBJECT_STATE_RUNNING != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "ProcessFrameDescriptor: camera not in running state (%d)", m_state.load() );
    }

    if ( QC_STATUS_OK == ret )
    {
        pCamFrameDesc = dynamic_cast<CameraFrameDescriptor *>( &bufDesc );
        if ( nullptr == pCamFrameDesc )
        {
            pCamMetaDataDesc = dynamic_cast<CameraMetaDataDescriptor *>( &bufDesc );
            if ( nullptr == pCamMetaDataDesc )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Invalid buffer descriptor" );
            }
            else
            {
                isFrameDesc = false;
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( true == isFrameDesc )
        {
            streamId = pCamFrameDesc->streamId;
            frameId = pCamFrameDesc->id;
            bufferHandle = pCamFrameDesc->dmaHandle;

            QC_TRACE_BEGIN( "Execute", { QCNodeTraceArg( "streamId", streamId ),
                                         QCNodeTraceArg( "frameId", frameId ) } );

            if ( m_frameBufferMap.find( bufferHandle ) == m_frameBufferMap.end() )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "ProcessFrameDescriptor(frame): buffer not registered "
                          "streamId=%u, frameId=%llu, dmaHandle=0x%llx",
                          streamId, frameId, bufferHandle );
            }

            if ( QC_STATUS_OK == ret )
            {
                if ( m_bRequestMode )
                {
                    ret = SubmitRequest( pCamFrameDesc );
                }
                else
                {
                    ret = ReleaseFrame( pCamFrameDesc );
                }
            }

            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Failed to process camera frame" );
            }

            QC_TRACE_END( "Execute", { QCNodeTraceArg( "streamId", streamId ),
                                       QCNodeTraceArg( "frameId", frameId ) } );
        }
        else
        {
            QC_TRACE_BEGIN( "Execute", { QCNodeTraceArg( "bufferIdx", bufferIdx ) } );

            if ( true == m_enableMetaData )
            {
                bufferIdx = pCamMetaDataDesc->id;
                ret = SubmitRequest( pCamMetaDataDesc );

                if ( QC_STATUS_OK != ret )
                {
                    QC_ERROR( "Failed to process camera metadata" );
                }
            }
            else
            {
                QC_ERROR( "Metadata flag is not enabled" );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }

            QC_TRACE_END( "Execute", { QCNodeTraceArg( "bufferIdx", bufferIdx ) } );
        }
    }

    return ret;
}

QCStatus_e CameraImpl::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    QCObjectState_e prevNodeStatus = m_state;

    QC_TRACE_BEGIN( "Stop", {} );

    if ( QC_OBJECT_STATE_RUNNING == m_state )
    {
        m_state = QC_OBJECT_STATE_STOPING;

        status = QCarCamStop( m_QcarCamHndl );
        if ( QCARCAM_RET_OK == status )
        {
            m_state = QC_OBJECT_STATE_READY;
            QC_INFO( "Qcarcam Stop call success" );
        }
        else
        {
            QC_ERROR( "Qcarcam could not be stopped: status=%d", status );
            m_state = prevNodeStatus;
            ret = QC_STATUS_FAIL;
        }
    }
    else
    {
        QC_ERROR( "Camera not in running state: %d", m_state.load() );
        ret = QC_STATUS_BAD_STATE;
    }

    QC_TRACE_END( "Stop", {} );

    return ret;
}

QCStatus_e CameraImpl::DeInitialize()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    QCObjectState_e prevNodeStatus = m_state;

    QC_TRACE_BEGIN( "DeInit", {} );

    if ( QC_OBJECT_STATE_READY != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Camera not in ready state: %d", m_state.load() );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_state = QC_OBJECT_STATE_DEINITIALIZING;

        if ( 0 == m_QcarCamHndl )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "Qcarcam null handle" );
        }

        status = QCarCamRelease( m_QcarCamHndl );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "QCarCamRelease failed %d", status );
        }

        status = QCarCamClose( m_QcarCamHndl );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "QCarCamClose failed %d", status );
        }

        if ( ( 0 != m_clientId ) && ( false == m_bIsPrimary ) )
        {
            ret = UnImportBuffers();
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Error in unimport buffers" );
            }
        }
    }

    ClearFrameBuffers();
    ClearMetaDataBuffers();
    if ( true != m_frameBufferMap.empty() )
    {
        m_frameBufferMap.clear();
    }
    if ( true != m_metaDataBufferMap.empty() )
    {
        m_metaDataBufferMap.clear();
    }

    if ( m_config.streamConfigs.size() > 0 )
    {
        m_config.streamConfigs.clear();
    }

    if ( QC_STATUS_OK == ret )
    {
        m_state = QC_OBJECT_STATE_INITIAL;
    }
    else
    {
        m_state = prevNodeStatus;
    }

    QC_TRACE_END( "DeInit", {} );

    return ret;
}

QCObjectState_e CameraImpl::GetState()
{
    return m_state;
}

QCStatus_e CameraImpl::ReleaseFrame( const CameraFrameDescriptor_t *pFrame )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    status = QCarCamReleaseFrame( m_QcarCamHndl, pFrame->streamId, pFrame->frameIdx );
    if ( QCARCAM_RET_OK == status )
    {
        QC_INFO( "QCarCamReleaseFrame success for index: %u", pFrame->frameIdx );
    }
    else
    {
        ret = QC_STATUS_FAIL;
        QC_ERROR( "QCarCamReleaseFrame fail for id: %d index: %u, status: %d", pFrame->streamId,
                  pFrame->frameIdx, status );
    }

    return ret;
}

QCStatus_e CameraImpl::SubmitRequest( const CameraFrameDescriptor_t *pFrame )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    QCarCamRequest_t request = { 0 };

    if ( m_enableInjection )
    {
        // In injection mode the injection thread owns the output-frame request lifecycle, so a
        // returned output frame is not re-submitted — it is released back to qcx so qcx can
        // accept the next QCarCamSubmitRequest from the injection thread.
        QC_DEBUG( "SubmitRequest(frame): injection mode — releasing output frame buffer "
                  "bufferListId=%u, bufferIdx=%u (injection thread drives re-submission)",
                  pFrame->streamId, pFrame->frameIdx );
        ret = ReleaseFrame( pFrame );
    }
    else if ( ( 0 != m_clientId ) && ( false == m_bIsPrimary ) )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "SubmitRequest for frame is not allowed for multi-client non primary session" );
    }
    else
    {
        uint32_t bufferListId = pFrame->streamId;
        uint32_t bufferIdx = pFrame->frameIdx;
        if ( true == m_bRequestPatternMode )
        {
            // Translate bufferListId to stream index for m_freeBufIdxQueue (indexed by stream
            // index)
            auto mapIt = m_streamIdToIndexMap.find( bufferListId );
            if ( mapIt == m_streamIdToIndexMap.end() )
            {
                ret = QC_STATUS_BAD_ARGUMENTS;
                QC_ERROR( "SubmitRequest(frame): bufferListId %u not found in stream map",
                          bufferListId );
            }
            else
            {
                uint32_t bufListStreamIndex = mapIt->second;
                uint32_t contextId = m_streamConfigs[bufListStreamIndex].contextId;
                std::unique_lock<std::mutex> lock( m_mutex );
                m_freeBufIdxQueue[bufListStreamIndex].push( bufferIdx );

                // Per-context streams requests packed for submission
                auto refIt = m_refStreamIdByContext.find( contextId );
                if ( ( refIt != m_refStreamIdByContext.end() ) &&
                     ( refIt->second == bufferListId ) )
                {
                    for ( uint32_t i = 0; i < m_streamNum; i++ )
                    {
                        if ( m_streamConfigs[i].contextId != contextId )
                        {
                            continue;
                        }

                        uint32_t streamId = m_streamConfigs[i].streamId;
                        if ( m_submitRequestPattern[i] > 0 )
                        {
                            m_submitRequestPattern[i]--;
                        }
                        if ( 0 == m_submitRequestPattern[i] )
                        {
                            if ( false == m_freeBufIdxQueue[i].empty() )
                            {
                                QCarCamStreamRequest_t *pStreamRequest =
                                        &request.streamRequests[request.numStreamRequests];
                                pStreamRequest->bufferlistId = streamId;
                                pStreamRequest->bufferIdx = m_freeBufIdxQueue[i].front();
                                m_freeBufIdxQueue[i].pop();
                                request.numStreamRequests++;
                                m_submitRequestPattern[i] = m_streamConfigs[i].submitRequestPattern;
                                QC_DEBUG( "SubmitRequest m_QcarCamHndl: %lu, contextId: %u, "
                                          "bufferlistId: %u, bufferIdx: %u",
                                          m_QcarCamHndl, contextId, pStreamRequest->bufferlistId,
                                          pStreamRequest->bufferIdx );
                            }
                        }
                    }
                }
            }
        }
        else
        {
            QCarCamStreamRequest_t *pStreamRequest = &request.streamRequests[0];
            pStreamRequest->bufferlistId = bufferListId;
            pStreamRequest->bufferIdx = bufferIdx;
            request.numStreamRequests = 1;
        }

        if ( ( QC_STATUS_OK == ret ) && ( request.numStreamRequests > 0 ) )
        {
            request.requestId = m_requestId.fetch_add( 1, std::memory_order_relaxed );

            QC_DEBUG( "SubmitRequest for frame begin, m_QcarCamHndl: %lu, bufferlistId: %u, "
                      "bufferIdx: %u, request id: %u",
                      m_QcarCamHndl, bufferListId, bufferIdx, request.requestId );

            status = QCarCamSubmitRequest( m_QcarCamHndl, &request );
            if ( QCARCAM_RET_OK != status )
            {
                ret = QC_STATUS_FAIL;
                QC_ERROR( "SubmitRequest for frame fail, m_QcarCamHndl: %lu, bufferlistId: %u, "
                          "bufferIdx: %u request id: %u, status=%d",
                          m_QcarCamHndl, bufferListId, bufferIdx, request.requestId, status );
            }
            else
            {
                QC_DEBUG( "SubmitRequest for frame success, m_QcarCamHndl: %lu, bufferlistId: %u, "
                          "bufferIdx: %u, request id: %u",
                          m_QcarCamHndl, bufferListId, bufferIdx, request.requestId );
            }
        }
    }

    return ret;
}

QCStatus_e CameraImpl::SubmitRequest( const CameraMetaDataDescriptor_t *pMetaData )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    uint32_t bufferListId = 0;
    uint32_t bufferIdx = 0;
    QCarCamRequest_t request = { 0 };

    if ( true == m_enableInjection )
    {
        request.requestId = pMetaData->requestId;
    }
    else
    {
        request.requestId = m_requestId.fetch_add( 1, std::memory_order_relaxed );
    }
    request.numStreamRequests = pMetaData->streamRequestNum;
    request.syncId = pMetaData->syncId;
    request.flags = pMetaData->flags;

    // Map injection input buffer
    request.inputBuffer.bufferlistId = pMetaData->inputBuffer.bufferListId;
    request.inputBuffer.bufferIdx = pMetaData->inputBuffer.bufferIdx;

    // Map common input metadata
    request.inputCommonMetadata.bufferlistId = pMetaData->inputCommonMetadata.bufferListId;
    request.inputCommonMetadata.bufferIdx = pMetaData->inputCommonMetadata.bufferIdx;

    // Map per-input metadata and output metadata
    for ( uint32_t i = 0; i < QCNODE_CAMERA_MAX_INPUT_STREAM_NUM; i++ )
    {
        request.inputMetadata[i].bufferlistId = pMetaData->inputMetadata[i].bufferListId;
        request.inputMetadata[i].bufferIdx = pMetaData->inputMetadata[i].bufferIdx;

        request.outputMetadata[i].bufferlistId = pMetaData->outputMetadata[i].bufferListId;
        request.outputMetadata[i].bufferIdx = pMetaData->outputMetadata[i].bufferIdx;
    }

    // Map per-stream output frame buffer requests
    for ( uint32_t i = 0; i < pMetaData->streamRequestNum; i++ )
    {
        request.streamRequests[i].bufferlistId = pMetaData->streamRequests[i].bufferListId;
        request.streamRequests[i].bufferIdx = pMetaData->streamRequests[i].bufferIdx;
        request.streamRequests[i].metaBufferlistId = pMetaData->streamRequests[i].metaBufferListId;
        request.streamRequests[i].metaBufferId = pMetaData->streamRequests[i].metaBufferId;
    }

    if ( QC_STATUS_OK == ret )
    {
        status = QCarCamSubmitRequest( m_QcarCamHndl, &request );
        if ( QCARCAM_RET_OK != status )
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "SubmitRequest for metadata fail, requestId: %u, status: %d",
                      request.requestId, status );
        }
        else
        {
            QC_INFO( "SubmitRequest for metadata success, requestId: %u, numStreamRequests: %u, "
                     "flags: 0x%x",
                     request.requestId, request.numStreamRequests, request.flags );
        }
    }

    return ret;
}

QCStatus_e CameraImpl::SetFrameBuffers(
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    uint32_t streamId = 0;
    uint32_t bufferIdx = 0;
    uint32_t bufferListId = 0;
    size_t bufferNum = 0;
    size_t bufferDescNum = buffers.size();
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offset = 0;
    uint64_t bufferHandle = 0;
    QCImageFormat_e format = QC_IMAGE_FORMAT_MAX;
    CameraFrameDescriptor_t *pCamFrameBuf = nullptr;
    QCarCamBuffer_t *pQcarCamBuf = nullptr;

    for ( size_t i = 0; i < m_streamNum; i++ )
    {
        streamId = m_streamConfigs[i].streamId;
        width = m_streamConfigs[i].width;
        height = m_streamConfigs[i].height;
        format = m_streamConfigs[i].format;
        bufferNum = m_streamConfigs[i].bufferIds.size();
        m_frameBuffers[i].pCamFrameDescs = new ( std::nothrow ) CameraFrameDescriptor_t[bufferNum];
        m_frameBuffers[i].pQcarCamFrameBuffers = new ( std::nothrow ) QCarCamBuffer_t[bufferNum];
        if ( ( nullptr == m_frameBuffers[i].pCamFrameDescs ) ||
             ( nullptr == m_frameBuffers[i].pQcarCamFrameBuffers ) )
        {
            QC_ERROR( "Failed to allocate frame buffers for stream %u (out of memory)", streamId );
            ret = QC_STATUS_NOMEM;
            break;
        }
        m_frameBuffers[i].bufferList.id = streamId;
        m_frameBuffers[i].bufferList.nBuffers = static_cast<uint32_t>( bufferNum );
        m_frameBuffers[i].bufferList.pBuffers = m_frameBuffers[i].pQcarCamFrameBuffers;
        m_frameBuffers[i].bufferList.colorFmt = GetQcarCamFormat( format );
        m_frameBuffers[i].bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;

        for ( uint32_t j = 0; j < bufferNum; j++ )
        {
            bufferIdx = m_streamConfigs[i].bufferIds[j];
            if ( bufferIdx >= bufferDescNum )
            {
                ret = QC_STATUS_OUT_OF_BOUND;
                QC_ERROR( "bufferIdx %u is out of range for stream %u", bufferIdx, streamId );
                break;
            }
            m_frameBuffers[i].pCamFrameDescs[j] = buffers[bufferIdx];
            pCamFrameBuf = &m_frameBuffers[i].pCamFrameDescs[j];
            pQcarCamBuf = &m_frameBuffers[i].pQcarCamFrameBuffers[j];
            pCamFrameBuf->streamId = streamId;
            pCamFrameBuf->frameIdx = j;

            if ( nullptr == pCamFrameBuf->pBuf )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Camera frame descriptor buffer is empty for stream %u, "
                          "bufferIdx %u",
                          streamId, bufferIdx );
                break;
            }

            bufferHandle = pCamFrameBuf->dmaHandle;
            if ( m_frameBufferMap.find( bufferHandle ) == m_frameBufferMap.end() )
            {
                m_frameBufferMap[bufferHandle] = m_frameBuffers[i];
            }
            else
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Camera frame descriptor buffer is registered for stream %u, "
                          "bufferIdx %u",
                          streamId, j );
                break;
            }

            if ( format != pCamFrameBuf->format )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Buffer property error for stream %u frame %u: image "
                          "format does not match, config: %u, buffer: %u",
                          streamId, bufferIdx, format, pCamFrameBuf->format );
                break;
            }

            if ( width != pCamFrameBuf->width )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Buffer property error for stream %u frame %u: image "
                          "width does not match, config: %u, buffer: %u",
                          streamId, bufferIdx, width, pCamFrameBuf->width );
                break;
            }

            if ( height != pCamFrameBuf->height )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Buffer property error for stream %u frame %u: image "
                          "height does not match, config: %u, buffer: %u",
                          streamId, bufferIdx, height, pCamFrameBuf->height );
                break;
            }

            offset = 0;
            pQcarCamBuf->numPlanes = pCamFrameBuf->numPlanes;

            if ( ( QC_IMAGE_FORMAT_NV12_UBWC == format ) ||
                 ( QC_IMAGE_FORMAT_TP10_UBWC == format ) )
            {
                pQcarCamBuf->numPlanes = 2;
            }
            if ( pQcarCamBuf->numPlanes > QCARCAM_MAX_NUM_PLANES )
            {
                pQcarCamBuf->numPlanes = QCARCAM_MAX_NUM_PLANES;
            }

            for ( uint32_t k = 0; k < pQcarCamBuf->numPlanes; k++ )
            {
                pQcarCamBuf->planes[k].memHndl = bufferHandle;
                pQcarCamBuf->planes[k].width = pCamFrameBuf->width;
                pQcarCamBuf->planes[k].height = pCamFrameBuf->height;
                pQcarCamBuf->planes[k].stride = pCamFrameBuf->stride[k];
                pQcarCamBuf->planes[k].size = pCamFrameBuf->planeBufSize[k];
                pQcarCamBuf->planes[k].offset = offset;
                offset += pCamFrameBuf->planeBufSize[k];
            }

            if ( ( QC_IMAGE_FORMAT_NV12 == format ) || ( QC_IMAGE_FORMAT_P010 == format ) ||
                 ( QC_IMAGE_FORMAT_NV12_UBWC == format ) ||
                 ( QC_IMAGE_FORMAT_TP10_UBWC == format ) )
            {
                pQcarCamBuf->planes[1].height /= 2;
            }

            if ( ( QC_IMAGE_FORMAT_NV12_UBWC == format ) ||
                 ( QC_IMAGE_FORMAT_TP10_UBWC == format ) )
            {
                pQcarCamBuf->planes[0].size =
                        pCamFrameBuf->planeBufSize[0] + pCamFrameBuf->planeBufSize[1];
                pQcarCamBuf->planes[0].offset = 0;
                pQcarCamBuf->planes[1].size =
                        pCamFrameBuf->planeBufSize[2] + pCamFrameBuf->planeBufSize[3];
                pQcarCamBuf->planes[1].offset = pQcarCamBuf->planes[0].size;
            }

            QC_DEBUG( "Set frame buffer index %u: memHndl: %llu, va: %p, width: %u, "
                      "height: %u, stride: %u size: %u",
                      i, pQcarCamBuf->planes[0].memHndl, pCamFrameBuf->pBuf,
                      pQcarCamBuf->planes[0].width, pQcarCamBuf->planes[0].height,
                      pQcarCamBuf->planes[0].stride, pQcarCamBuf->planes[0].size );
        }

        if ( QC_STATUS_OK == ret )
        {
            status = QCarCamSetBuffers(
                    m_QcarCamHndl, (const QCarCamBufferList_t *) &m_frameBuffers[i].bufferList );
            if ( QCARCAM_RET_OK != status )
            {
                ret = QC_STATUS_FAIL;
                QC_ERROR( "Failed to set QCarCam buffers for frame, streamId: %u, "
                          "status: %d",
                          streamId, status );
                break;
            }
        }
        else
        {
            QC_ERROR( "Failed to set frame buffer for stream %u", streamId );
            break;
        }
    }

    if ( QC_STATUS_OK != ret )
    {
        ClearFrameBuffers();
    }

    return ret;
}

QCStatus_e CameraImpl::RegisterInjectionBufferList(
        const std::vector<uint32_t> &bufIds, uint32_t listId, const char *label,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
        std::vector<CameraMetaDataBuffers_t> &injectionBuffers, QCarCamColorFmt_e colorFmt,
        uint32_t planeWidth, uint32_t planeHeight, uint32_t planeStride )
{
    QCStatus_e ret = QC_STATUS_OK;
    size_t numBufs = bufIds.size();
    size_t bufferDescNum = buffers.size();
    CameraMetaDataBuffers_t bufs;

    if ( 0 == numBufs )
    {
        QC_DEBUG( "RegisterInjectionBufferList: no %s buffers configured", label );
    }
    else
    {
        bufs.pCamMetaDataDescs = new ( std::nothrow ) BufferDescriptor_t[numBufs];
        bufs.pQcarCamMetaDataBuffers = new ( std::nothrow ) QCarCamBuffer_t[numBufs];
        if ( ( nullptr == bufs.pCamMetaDataDescs ) || ( nullptr == bufs.pQcarCamMetaDataBuffers ) )
        {
            QC_ERROR( "Failed to allocate injection buffers for list %u (out of memory)", listId );
            ret = QC_STATUS_NOMEM;
        }

        if ( QC_STATUS_OK == ret )
        {
            bufs.bufferList.id = listId;
            bufs.bufferList.nBuffers = (uint32_t) numBufs;
            bufs.bufferList.pBuffers = bufs.pQcarCamMetaDataBuffers;
            bufs.bufferList.colorFmt = colorFmt;
            bufs.bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;

            for ( size_t k = 0; ( QC_STATUS_OK == ret ) && ( k < numBufs ); k++ )
            {
                uint32_t bIdx = bufIds[k];
                if ( bIdx >= (uint32_t) bufferDescNum )
                {
                    ret = QC_STATUS_OUT_OF_BOUND;
                    QC_ERROR( "RegisterInjectionBufferList: %s bufferIdx %u is out of range", label,
                              bIdx );
                }
                else
                {
                    bufs.pCamMetaDataDescs[k] = buffers[bIdx];
                    BufferDescriptor_t *pBuf = &bufs.pCamMetaDataDescs[k];
                    QCarCamBuffer_t *pQBuf = &bufs.pQcarCamMetaDataBuffers[k];

                    if ( nullptr == pBuf->pBuf )
                    {
                        ret = QC_STATUS_INVALID_BUF;
                        QC_ERROR( "RegisterInjectionBufferList: %s buffer is empty, bufferIdx %u",
                                  label, bIdx );
                    }
                    else
                    {
                        pQBuf->numPlanes = 1;
                        pQBuf->planes[0].size = (uint32_t) pBuf->size;
                        pQBuf->planes[0].memHndl = pBuf->dmaHandle;
                        pQBuf->planes[0].offset = 0;
                        pQBuf->planes[0].width = planeWidth;
                        pQBuf->planes[0].height = planeHeight;
                        pQBuf->planes[0].stride = planeStride;

                        QC_DEBUG( "RegisterInjectionBufferList: Set %s buffer %u: memHndl: %llu, "
                                  "va: %p, size: %u, width: %u, height: %u, stride: %u",
                                  label, (uint32_t) k, pBuf->dmaHandle, pBuf->pBuf,
                                  pQBuf->planes[0].size, planeWidth, planeHeight, planeStride );
                    }
                }
            }

            if ( QC_STATUS_OK == ret )
            {
                QCarCamRet_e qret = QCarCamSetBuffers(
                        m_QcarCamHndl, (const QCarCamBufferList_t *) &bufs.bufferList );
                if ( QCARCAM_RET_OK == qret )
                {
                    QC_INFO( "RegisterInjectionBufferList: QCarCamSetBuffers success for %s "
                             "buffers, bufferListId: %u, bufferNum: %u",
                             label, listId, (uint32_t) numBufs );
                    injectionBuffers.push_back( std::move( bufs ) );
                    /* ownership transferred — do not free here */
                    bufs.pCamMetaDataDescs = nullptr;
                    bufs.pQcarCamMetaDataBuffers = nullptr;
                }
                else
                {
                    ret = QC_STATUS_FAIL;
                    QC_ERROR( "RegisterInjectionBufferList: Failed to set QCarCam %s buffers, "
                              "bufferListId: %u, status: %d",
                              label, listId, (int) qret );
                }
            }
        }

        /* Single consolidated cleanup: free any buffers not yet transferred.
         * Covers: (a) alloc failed (NOMEM), (b) loop error (OUT_OF_BOUND /
         * INVALID_BUF), (c) QCarCamSetBuffers failed.  On success the pointers
         * were nulled above so this is a no-op. */
        if ( nullptr != bufs.pCamMetaDataDescs )
        {
            delete[] bufs.pCamMetaDataDescs;
            bufs.pCamMetaDataDescs = nullptr;
        }
        if ( nullptr != bufs.pQcarCamMetaDataBuffers )
        {
            delete[] bufs.pQcarCamMetaDataBuffers;
            bufs.pQcarCamMetaDataBuffers = nullptr;
        }
    }

    return ret;
}

QCStatus_e CameraImpl::SetMetaDataBuffers(
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    uint32_t bufferIdx = 0;
    uint32_t bufferListId = 0;
    uint32_t bufferListType = 0;
    size_t bufferNum = 0;
    size_t bufferDescNum = buffers.size();
    uint64_t bufferHandle = 0;
    BufferDescriptor_t *pCamMetaDataBuf = nullptr;
    QCarCamBuffer_t *pQcarCamBuf = nullptr;
    std::vector<CameraMetaDataBuffers_t> injBufs;

    for ( size_t i = 0; i < m_metaDataNum; i++ )
    {
        CameraMetaDataConfig_t &metaDataConfig = m_config.metaDataConfigs[i];
        bufferListId = metaDataConfig.bufferListId;
        bufferNum = metaDataConfig.bufferIds.size();
        m_metaDataBuffers[i].pCamMetaDataDescs = new ( std::nothrow ) BufferDescriptor_t[bufferNum];
        m_metaDataBuffers[i].pQcarCamMetaDataBuffers =
                new ( std::nothrow ) QCarCamBuffer_t[bufferNum];
        if ( ( nullptr == m_metaDataBuffers[i].pCamMetaDataDescs ) ||
             ( nullptr == m_metaDataBuffers[i].pQcarCamMetaDataBuffers ) )
        {
            QC_ERROR( "Failed to allocate metadata buffers for list %u (out of memory)",
                      bufferListId );
            ret = QC_STATUS_NOMEM;
            break;
        }
        m_metaDataBuffers[i].bufferList.id = bufferListId;
        m_metaDataBuffers[i].bufferList.nBuffers = (uint32_t) bufferNum;
        m_metaDataBuffers[i].bufferList.pBuffers = m_metaDataBuffers[i].pQcarCamMetaDataBuffers;
        m_metaDataBuffers[i].bufferList.colorFmt = QCARCAM_FMT_MAX;
        m_metaDataBuffers[i].bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;

        bufferListType = QCARCAM_GET_BUFFERLIST_TYPE( bufferListId );

        for ( size_t k = 0; k < bufferNum; k++ )
        {
            bufferIdx = metaDataConfig.bufferIds[k];
            if ( bufferIdx >= bufferDescNum )
            {
                ret = QC_STATUS_OUT_OF_BOUND;
                QC_ERROR( "bufferIdx %u is out of range for metadata %u", bufferIdx, i );
                break;
            }
            m_metaDataBuffers[i].pCamMetaDataDescs[k] = buffers[bufferIdx];
            pCamMetaDataBuf = &m_metaDataBuffers[i].pCamMetaDataDescs[k];
            pQcarCamBuf = &m_metaDataBuffers[i].pQcarCamMetaDataBuffers[k];

            if ( nullptr == pCamMetaDataBuf->pBuf )
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Camera metadata descriptor buffer is empty for bufferList %u, "
                          "bufferIdx %u",
                          bufferListId, bufferIdx );
                break;
            }

            bufferHandle = pCamMetaDataBuf->dmaHandle;
            if ( m_metaDataBufferMap.find( bufferHandle ) == m_metaDataBufferMap.end() )
            {
                m_metaDataBufferMap[bufferHandle] = m_metaDataBuffers[i];
            }
            else
            {
                ret = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Camera metadata descriptor buffer is registered, bufferList: %u, "
                          "bufferIdx: %u",
                          bufferListId, bufferIdx );
                break;
            }

            pQcarCamBuf->numPlanes = 1;
            pQcarCamBuf->planes[0].size = (uint32_t) pCamMetaDataBuf->size;
            pQcarCamBuf->planes[0].memHndl = bufferHandle;
            pQcarCamBuf->planes[0].offset = 0;

            if ( QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA == bufferListType )
            {
                QC_DEBUG( "Set metadata buffer (pre-initialised), metaDataType: %d, "
                          "bufferList: %u, bufferIdx: %u",
                          (int) metaDataConfig.metaDataType, bufferListId, bufferIdx );
            }
            else
            {
                QC_DEBUG( "Set injection metadata buffer, bufferList: %u, bufferIdx: %u, "
                          "memHndl: %llu, va: %p, size: %u",
                          bufferListId, bufferIdx, bufferHandle, pCamMetaDataBuf->pBuf,
                          pQcarCamBuf->planes[0].size );
            }
        }

        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Failed to set metadata buffer for metadata %u", i );
            break;
        }

        status = QCarCamSetBuffers(
                m_QcarCamHndl, (const QCarCamBufferList_t *) &m_metaDataBuffers[i].bufferList );
        if ( QCARCAM_RET_OK == status )
        {
            QC_INFO( "QCarCamSetBuffers success for metadata %u, metaDataType: %d, "
                     "bufferList: %u, bufferNum: %u",
                     i, (int) metaDataConfig.metaDataType, bufferListId, (uint32_t) bufferNum );
        }
        else
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "Failed to set QCarCam buffers for metadata %u, bufferListId: %u, "
                      "status: %d",
                      i, bufferListId, status );
            break;
        }

        if ( ( QC_STATUS_OK == ret ) &&
             ( CAMERA_METADATA_TYPE_ISP_INJECTION == metaDataConfig.metaDataType ) )
        {
            const CameraInjectConfig_t &injCfg = metaDataConfig.injectionConfig;

            // Raw frame input buffers (QCARCAM_BUFFERLIST_TYPE_INPUT)
            if ( QC_STATUS_OK == ret )
            {
                QC_INFO( "SetMetaDataBuffers: injection input colorFmt=0x%x, "
                         "width=%u, height=%u, stride=%u",
                         (uint32_t) injCfg.format, injCfg.width, injCfg.height, injCfg.stride );
                ret = RegisterInjectionBufferList(
                        injCfg.inputBufferIds, injCfg.inputBufferListId, "injection_input", buffers,
                        injBufs, injCfg.format, injCfg.width, injCfg.height, injCfg.stride );
            }

            // Sensor header / per-frame metadata buffers (INPUT_METADATA)
            if ( ( QC_STATUS_OK == ret ) && ( injCfg.headerBufferIds.size() > 0 ) )
            {
                ret = RegisterInjectionBufferList( injCfg.headerBufferIds,
                                                   injCfg.headerBufferListId, "injection_header",
                                                   buffers, injBufs );
            }

            // EEPROM calibration buffers (INPUT_METADATA, separate list)
            if ( ( QC_STATUS_OK == ret ) && ( injCfg.eepromBufferIds.size() > 0 ) )
            {
                ret = RegisterInjectionBufferList( injCfg.eepromBufferIds,
                                                   injCfg.eepromBufferListId, "injection_eeprom",
                                                   buffers, injBufs );
            }
        }

        // Register output metadata buffers if outputBufferListId is set
        if ( ( QC_STATUS_OK == ret ) && ( 0 != metaDataConfig.outputBufferListId ) &&
             ( !metaDataConfig.outputBufferIds.empty() ) )
        {
            ret = RegisterInjectionBufferList( metaDataConfig.outputBufferIds,
                                               metaDataConfig.outputBufferListId, "output_metadata",
                                               buffers, injBufs );
            if ( QC_STATUS_OK == ret )
            {
                QC_INFO( "SetMetaDataBuffers: registered %u output metadata buffers "
                         "(bufferListId=0x%x) for metadata %u (metaDataType=%d)",
                         (uint32_t) metaDataConfig.outputBufferIds.size(),
                         metaDataConfig.outputBufferListId, i, (int) metaDataConfig.metaDataType );
            }
        }

        if ( QC_STATUS_OK != ret )
        {
            break;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        // Commit injection-list ownership to the session-lived m_metaDataBuffers so the
        // arrays handed to QCarCamSetBuffers stay alive for the duration of the session.
        // ClearMetaDataBuffers() will release them in DeInitialize / failure rollback.
        for ( CameraMetaDataBuffers_t &entry : injBufs )
        {
            m_metaDataBuffers.push_back( std::move( entry ) );
        }
        injBufs.clear();
    }
    else
    {
        // Locally-owned arrays must be released explicitly — CameraMetaDataBuffers_t
        // is a POD-style struct with no destructor.
        for ( CameraMetaDataBuffers_t &entry : injBufs )
        {
            delete[] entry.pCamMetaDataDescs;
            entry.pCamMetaDataDescs = nullptr;
            delete[] entry.pQcarCamMetaDataBuffers;
            entry.pQcarCamMetaDataBuffers = nullptr;
        }
        injBufs.clear();

        ClearMetaDataBuffers();
    }

    return ret;
}

QCStatus_e CameraImpl::SubmitAllBuffers()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    // For injection usecase, initial buffers are submitted by the injection thread
    if ( m_enableInjection )
    {
        QC_INFO( "SubmitAllBuffers: injection mode, skipping initial frame buffer submission" );
        return ret;
    }

    if ( ( ( 0 == m_clientId ) || ( true == m_bIsPrimary ) ) )
    {
        for ( uint32_t bufIdx = 0; ( QC_STATUS_OK == ret ) && ( bufIdx < m_maxBufCnt ); bufIdx++ )
        {
            // Group streams by contextId so each QCarCamSubmitRequest carries streams from a
            // single context only. The qcx server rejects mixed-context streamRequests[] with
            // "All the streams in the request are not part of same context".
            std::unordered_map<uint32_t, QCarCamRequest_t> requestByContext;
            for ( uint32_t i = 0; i < m_streamNum; i++ )
            {
                size_t bufferNum = m_streamConfigs[i].bufferIds.size();
                if ( bufIdx < bufferNum )
                {
                    uint32_t ctxId = m_streamConfigs[i].contextId;
                    QCarCamRequest_t &request = requestByContext[ctxId];
                    QCarCamStreamRequest_t *pStreamRequest =
                            &request.streamRequests[request.numStreamRequests];
                    pStreamRequest->bufferlistId = m_streamConfigs[i].streamId;
                    pStreamRequest->bufferIdx = bufIdx;
                    request.numStreamRequests++;
                    QC_DEBUG( "Submit request for QcarCamHndl: %lu, contextId: %u, "
                              "bufferlistId: %u, bufferIdx: %u",
                              m_QcarCamHndl, ctxId, pStreamRequest->bufferlistId,
                              pStreamRequest->bufferIdx );
                }
            }

            for ( auto &entry : requestByContext )
            {
                QCarCamRequest_t &request = entry.second;
                request.requestId = m_requestId.fetch_add( 1, std::memory_order_relaxed );

                // Attach common input metadata for TUNING_FEATURE sticky modes
                if ( ( false == m_enableInjection ) &&
                     ( 0 != m_currentInputCommonMetadata.bufferListId ) )
                {
                    request.inputCommonMetadata.bufferlistId =
                            m_currentInputCommonMetadata.bufferListId;
                    request.inputCommonMetadata.bufferIdx = m_currentInputCommonMetadata.bufferIdx;
                }

                status = QCarCamSubmitRequest( m_QcarCamHndl, &request );
                if ( QCARCAM_RET_OK != status )
                {
                    QC_ERROR( "Failed to submit request for QcarCamHndl: %lu, contextId: %u, "
                              "bufferIdx: %u, request id: %u, status=%d",
                              m_QcarCamHndl, entry.first, bufIdx, request.requestId, status );
                    ret = QC_STATUS_FAIL;
                    break;
                }
            }
        }
    }

    return ret;
}

QCStatus_e CameraImpl::ImportBuffers()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    MemUtils memUtils;
    uint32_t streamId = 0;
    uint64_t bufferHandle = 0;
    size_t bufferNum = 0;

    CameraFrameDescriptor_t *pCamFrame = nullptr;
    QCarCamBuffer_t *pQcarcamBuf = nullptr;

    for ( size_t i = 0; i < m_streamNum; i++ )
    {
        streamId = m_streamConfigs[i].streamId;
        bufferNum = m_streamConfigs[i].bufferIds.size();

        m_frameBuffers[i].pCamFrameDescs = new ( std::nothrow ) CameraFrameDescriptor_t[bufferNum];
        m_frameBuffers[i].pQcarCamFrameBuffers = new ( std::nothrow ) QCarCamBuffer_t[bufferNum];
        if ( ( nullptr == m_frameBuffers[i].pCamFrameDescs ) ||
             ( nullptr == m_frameBuffers[i].pQcarCamFrameBuffers ) )
        {
            QC_ERROR( "Failed to allocate import frame buffers for stream %u (out of memory)",
                      streamId );
            ret = QC_STATUS_NOMEM;
            break;
        }
        m_frameBuffers[i].bufferList.id = streamId;
        m_frameBuffers[i].bufferList.nBuffers = (uint32_t) bufferNum;
        m_frameBuffers[i].bufferList.pBuffers = m_frameBuffers[i].pQcarCamFrameBuffers;
        m_frameBuffers[i].bufferList.colorFmt = GetQcarCamFormat( m_streamConfigs[i].format );
        m_frameBuffers[i].bufferList.flags = QCARCAM_BUFFER_FLAG_OS_HNDL;

        // get buffers
        status = QCarCamGetBuffers( m_QcarCamHndl, &m_frameBuffers[i].bufferList );
        if ( QCARCAM_RET_OK == status )
        {
            QC_INFO( "QCarCamGetBuffers successful" );
            for ( uint32_t j = 0; j < bufferNum; j++ )
            {
                CameraFrameDescriptor_t camFrameDesc;
                pCamFrame = &m_frameBuffers[i].pCamFrameDescs[j];
                pQcarcamBuf = &m_frameBuffers[i].pQcarCamFrameBuffers[j];

                camFrameDesc.pid = 0;
                camFrameDesc.size = 0;
                camFrameDesc.offset = 0;
                camFrameDesc.type = QC_BUFFER_TYPE_IMAGE;
                camFrameDesc.cache = QC_CACHEABLE;
                camFrameDesc.dmaHandle = pQcarcamBuf->planes[0].memHndl;
                camFrameDesc.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;

                camFrameDesc.batchSize = 1;
                camFrameDesc.format = m_streamConfigs[i].format;
                camFrameDesc.numPlanes = pQcarcamBuf->numPlanes;
                camFrameDesc.width = pQcarcamBuf->planes[0].width;
                camFrameDesc.height = pQcarcamBuf->planes[0].height;
                bufferHandle = camFrameDesc.dmaHandle;

                if ( m_frameBufferMap.find( bufferHandle ) == m_frameBufferMap.end() )
                {
                    m_frameBufferMap[bufferHandle] = m_frameBuffers[i];
                }
                else
                {
                    ret = QC_STATUS_INVALID_BUF;
                    QC_ERROR( "Camera frame descriptor buffer is registered for stream %u, "
                              "bufferIdx %u",
                              streamId, j );
                    break;
                }

                for ( uint32_t k = 0; k < pQcarcamBuf->numPlanes; k++ )
                {
                    camFrameDesc.stride[k] = pQcarcamBuf->planes[k].stride;
                    camFrameDesc.planeBufSize[k] = pQcarcamBuf->planes[k].size;
                    camFrameDesc.actualHeight[k] =
                            pQcarcamBuf->planes[k].size / pQcarcamBuf->planes[k].stride;
                    camFrameDesc.size += pQcarcamBuf->planes[k].size;
                }

                ret = memUtils.MemoryMap( camFrameDesc, *pCamFrame );
                if ( QC_STATUS_OK == ret )
                {
                    void *pBuf = pCamFrame->pBuf;
                    *pCamFrame = camFrameDesc;
                    pCamFrame->pBuf = pBuf;
                    pCamFrame->frameIdx = j;
                    pCamFrame->streamId = streamId;
                    QC_INFO( "buffer index %u: memHndl: %llu va: %p width: %u, "
                             "height: %u, stride: %u size: %u",
                             i, pQcarcamBuf->planes[0].memHndl, pCamFrame->pBuf,
                             pQcarcamBuf->planes[0].width, pQcarcamBuf->planes[0].height,
                             pQcarcamBuf->planes[0].stride, pQcarcamBuf->planes[0].size );
                }
                else
                {
                    QC_ERROR( "Failed to import frame memory for stream %u buffer %u", i, j );
                    break;
                }
            }
        }
        else
        {
            ret = QC_STATUS_FAIL;
            ClearFrameBuffers();
            QC_ERROR( "QCarCamGetBuffers error ret %d  handle %lu", status, m_QcarCamHndl );
        }
    }

    return ret;
}

QCStatus_e CameraImpl::UnImportBuffers()
{
    QCStatus_e ret = QC_STATUS_OK;

    MemUtils memUtils;
    uint32_t streamId = 0;
    CameraFrameDescriptor_t *pCamFrame = nullptr;

    for ( uint32_t i = 0; i < m_streamNum; i++ )
    {
        streamId = m_streamConfigs[i].streamId;
        size_t bufferNum = m_streamConfigs[i].bufferIds.size();

        if ( i >= m_frameBuffers.size() )
        {
            break;
        }

        if ( nullptr != m_frameBuffers[i].pCamFrameDescs )
        {
            for ( size_t k = 0; k < bufferNum; k++ )
            {
                pCamFrame = &m_frameBuffers[i].pCamFrameDescs[k];
                ret = memUtils.MemoryUnMap( *pCamFrame );
                if ( QC_STATUS_OK != ret )
                {
                    QC_ERROR( "Failed to unimport frame memory for stream %u buffer %u", i, k );
                }
            }
        }
        else
        {
            ret = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "m_pCameraFrames is nullptr" );
        }
    }

    return ret;
}

void CameraImpl::ClearFrameBuffers()
{
    if ( m_frameBuffers.size() > 0 )
    {
        for ( uint32_t i = 0; i < m_frameBuffers.size(); i++ )
        {
            if ( nullptr != m_frameBuffers[i].pCamFrameDescs )
            {
                delete[] m_frameBuffers[i].pCamFrameDescs;
                m_frameBuffers[i].pCamFrameDescs = nullptr;
            }

            if ( nullptr != m_frameBuffers[i].pQcarCamFrameBuffers )
            {
                delete[] m_frameBuffers[i].pQcarCamFrameBuffers;
                m_frameBuffers[i].pQcarCamFrameBuffers = nullptr;
            }
        }
        m_frameBuffers.clear();
    }
}

void CameraImpl::ClearMetaDataBuffers()
{
    if ( m_metaDataBuffers.size() > 0 )
    {
        for ( uint32_t i = 0; i < m_metaDataBuffers.size(); i++ )
        {
            if ( nullptr != m_metaDataBuffers[i].pCamMetaDataDescs )
            {
                delete[] m_metaDataBuffers[i].pCamMetaDataDescs;
                m_metaDataBuffers[i].pCamMetaDataDescs = nullptr;
            }

            if ( nullptr != m_metaDataBuffers[i].pQcarCamMetaDataBuffers )
            {
                delete[] m_metaDataBuffers[i].pQcarCamMetaDataBuffers;
                m_metaDataBuffers[i].pQcarCamMetaDataBuffers = nullptr;
            }
        }
        m_metaDataBuffers.clear();
    }
}

QCStatus_e CameraImpl::QueryInputs()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    uint32_t inputCount = 0;
    uint32_t queryCount = 0;
    s_cameraInputsInfo.numInputs = 0;

    do
    {
        status = QCarCamQueryInputs( NULL, 0, &inputCount );
        if ( ( QCARCAM_RET_OK != status ) || ( 0 == inputCount ) )
        {
            queryCount++;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        else
        {
            break;
        }
    } while ( ( 0 == inputCount ) && ( queryCount < MAX_QUERY_TIMES ) );

    if ( QC_STATUS_OK == ret )
    {
        if ( 0 == inputCount )
        {
            ret = QC_STATUS_FAIL;
            QC_LOG_ERROR( "Didn't detect any camera connection" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        s_cameraInputsInfo.pCameraInputs = new ( std::nothrow ) QCarCamInput_t[inputCount];
        s_cameraInputsInfo.pCamInputModes = new ( std::nothrow ) QCarCamInputModes_t[inputCount];
        if ( ( nullptr == s_cameraInputsInfo.pCameraInputs ) ||
             ( nullptr == s_cameraInputsInfo.pCamInputModes ) )
        {
            QC_LOG_ERROR( "Failed to allocate camera input arrays (out of memory)" );
            ret = QC_STATUS_NOMEM;
        }
        if ( QC_STATUS_OK == ret )
        {
            (void) memset( s_cameraInputsInfo.pCamInputModes, 0,
                           sizeof( QCarCamInputModes_t ) * inputCount );

            status = QCarCamQueryInputs( s_cameraInputsInfo.pCameraInputs, inputCount,
                                         &s_cameraInputsInfo.numInputs );

            if ( ( QCARCAM_RET_OK != status ) || ( s_cameraInputsInfo.numInputs != inputCount ) )
            {
                QC_LOG_ERROR( "Query failed QCarCamQueryInputs %u %u: ret = %d",
                              s_cameraInputsInfo.numInputs, inputCount, status );
                ret = QC_STATUS_FAIL;
            }
            else
            {
                for ( uint32_t i = 0; i < inputCount; i++ )
                {
                    QC_LOG_INFO( "Available camera input id: %u, numModes = %u",
                                 s_cameraInputsInfo.pCameraInputs[i].inputId,
                                 s_cameraInputsInfo.pCameraInputs[i].numModes );

                    s_cameraInputsInfo.pCamInputModes[i].pModes = new ( std::nothrow )
                            QCarCamMode_t[s_cameraInputsInfo.pCameraInputs[i].numModes];
                    if ( nullptr == s_cameraInputsInfo.pCamInputModes[i].pModes )
                    {
                        QC_LOG_ERROR( "Failed to allocate modes for input %u (out of memory)",
                                      s_cameraInputsInfo.pCameraInputs[i].inputId );
                        ret = QC_STATUS_NOMEM;
                        break;
                    }
                    s_cameraInputsInfo.pCamInputModes[i].numModes =
                            s_cameraInputsInfo.pCameraInputs[i].numModes;

                    status = QCarCamQueryInputModes( s_cameraInputsInfo.pCameraInputs[i].inputId,
                                                     &s_cameraInputsInfo.pCamInputModes[i] );
                    if ( QCARCAM_RET_OK != status )
                    {
                        ret = QC_STATUS_FAIL;
                        QC_LOG_ERROR( "Query Input Modes failed for input %u: ret = %d",
                                      s_cameraInputsInfo.pCameraInputs[i].inputId, status );
                        break;
                    }
                    else
                    {
                        QC_LOG_INFO(
                                "Found camera with input %du: mode 0 src 0: "
                                "resolution %ux%u",
                                s_cameraInputsInfo.pCameraInputs[i].inputId,
                                s_cameraInputsInfo.pCamInputModes[i].pModes[0].sources[0].width,
                                s_cameraInputsInfo.pCamInputModes[i].pModes[0].sources[0].height );
                    }
                }
            }

            if ( QC_STATUS_OK != ret )
            {
                FreeCameraInputsInfo();
            }
        }
    }

    return ret;
}

QCStatus_e CameraImpl::GetInputsInfo( CameraInputs_t *pCamInputs )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( nullptr == pCamInputs )
    {
        QC_LOG_ERROR( "pCamInputs is nullptr" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( nullptr == s_cameraInputsInfo.pCameraInputs )
    {
        QC_LOG_ERROR( "Error in camera inputs" );
        pCamInputs->numInputs = 0;
        ret = QC_STATUS_FAIL;
    }
    else
    {
        *pCamInputs = s_cameraInputsInfo;
    }

    return ret;
}

QCStatus_e CameraImpl::GetFrame( const QCarCamFrameInfo_t &camFrameInfo, uint32_t &bufferListId,
                                 uint32_t &bufferIdx )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;

    bufferListId = camFrameInfo.id;
    bufferIdx = camFrameInfo.bufferIndex;
    uint64_t timeout = 0;
    QCarCamFrameInfo_t frameInfo = { 0 };
    frameInfo.id = bufferListId;
    CameraFrameDescriptor_t *pCamFrame = nullptr;
    uint32_t streamIndex = 0;

    auto mapIt = m_streamIdToIndexMap.find( bufferListId );
    if ( mapIt == m_streamIdToIndexMap.end() )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "GetFrame: bufferListId %u not found in stream map", bufferListId );
    }
    else
    {
        streamIndex = mapIt->second;
    }

    if ( ( QC_STATUS_OK == ret ) && ( true == m_bRequestMode ) )
    {
        if ( bufferIdx >= m_frameBuffers[streamIndex].bufferList.nBuffers )
        {
            ret = QC_STATUS_OUT_OF_BOUND;
            QC_ERROR( "Buffer index %u out of range for bufferListId %u", bufferIdx, bufferListId );
        }
        else
        {
            pCamFrame = &m_frameBuffers[streamIndex].pCamFrameDescs[bufferIdx];
            pCamFrame->timestamp = camFrameInfo.sofTimestamp.timestamp;
            pCamFrame->timestampQGPTP = camFrameInfo.sofTimestamp.timestampGPTP;
            pCamFrame->flags = camFrameInfo.flags;
            QC_DEBUG( "Get Camera Frame Info: "
                      "bufferListId: %u "
                      "buffer index: %u "
                      "buffer pointer addr: %p "
                      "buffer data addr: %p "
                      "buffer size: %u "
                      "timestamp: %llu "
                      "timestampGPTP: %llu "
                      "flags: %x ",
                      bufferListId, bufferIdx, pCamFrame, pCamFrame->pBuf, pCamFrame->size,
                      pCamFrame->timestamp, pCamFrame->timestampQGPTP, pCamFrame->flags );
        }
    }
    else if ( QC_STATUS_OK == ret )
    {
        status = QCarCamGetFrame( m_QcarCamHndl, &frameInfo, timeout, 0 );
        if ( QCARCAM_RET_OK == status )
        {
            bufferIdx = frameInfo.bufferIndex;
            if ( bufferIdx >= m_frameBuffers[streamIndex].bufferList.nBuffers )
            {
                ret = QC_STATUS_OUT_OF_BOUND;
                QC_ERROR( "Buffer index %u out of range for bufferListId %u", bufferIdx,
                          bufferListId );
            }
            else
            {
                pCamFrame = &m_frameBuffers[streamIndex].pCamFrameDescs[bufferIdx];
                if ( pCamFrame == nullptr )
                {
                    ret = QC_STATUS_FAIL;
                    QC_ERROR( "CameraFrameDescriptor pointer is nullptr, bufferListId: %u, "
                              "bufferIdx: %u",
                              bufferListId, bufferIdx );
                }
                else
                {
                    pCamFrame->timestamp = frameInfo.sofTimestamp.timestamp;
                    pCamFrame->timestampQGPTP = frameInfo.sofTimestamp.timestampGPTP;
                    pCamFrame->flags = frameInfo.flags;
                    QC_DEBUG( "Get Camera Frame Info: "
                              "bufferListId: %u "
                              "buffer index: %u "
                              "buffer pointer addr: %p "
                              "buffer data addr: %p "
                              "buffer size: %u "
                              "timestamp: %llu "
                              "timestampGPTP: %llu "
                              "flags: %x ",
                              bufferListId, bufferIdx, pCamFrame, pCamFrame->pBuf, pCamFrame->size,
                              pCamFrame->timestamp, pCamFrame->timestampQGPTP, pCamFrame->flags );
                }
            }
        }
        else
        {
            ret = QC_STATUS_FAIL;
            QC_ERROR( "QCarCamGetFrame failed, m_QcarCamHndl: %lu, status=%d", m_QcarCamHndl,
                      status );
        }
    }

    return ret;
}

QCStatus_e CameraImpl::ValidateConfig( const CameraImplConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    size_t streamNum = config.streamConfigs.size();

    if ( ( streamNum > QCNODE_CAMERA_MAX_STREAM_NUM ) || ( 0 == streamNum ) )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Invalid stream number: %u", streamNum );
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( ( true == config.bPrimary ) && ( 0 == config.clientId ) )
        {
            ret = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Invalid client id for primary session" );
        }
    }

    return ret;
}

void CameraImpl::ReturnInvalidFrame( CameraFrameDescriptor_t &frame )
{
    // A frame-ready event arrived with QCARCAM_BUFFER_STATUS_INVALID set.
    QCStatus_e ret = QC_STATUS_OK;

    if ( false == m_enableInjection )
    {
        if ( true == m_bRequestMode )
        {
            ret = SubmitRequest( &frame );
        }
        else
        {
            ret = ReleaseFrame( &frame );
        }

        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR(
                    "ReturnInvalidFrame: failed to return buffer streamId=%u, frameIdx=%u, ret=%d",
                    frame.streamId, frame.frameIdx, ret );
        }
    }
}

void CameraImpl::FrameCallback( CameraFrameDescriptor_t *pFrame )
{
    QCStatus_e ret = QC_STATUS_OK;
    NodeFrameDescriptor frameDesc( 1 );

    if ( nullptr == m_callback )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "callback is invalid" );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = frameDesc.SetBuffer( 0, *pFrame );
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( QC_OBJECT_STATE_RUNNING == m_state )
        {
            // Translate streamId to compact stream index for m_frameId array access.
            auto mapIt = m_streamIdToIndexMap.find( pFrame->streamId );
            uint32_t streamIndex = ( mapIt != m_streamIdToIndexMap.end() ) ? mapIt->second : 0U;
            QC_TRACE_EVENT( "FrameReady", { QCNodeTraceArg( "streamId", pFrame->streamId ),
                                            QCNodeTraceArg( "frameId", m_frameId[streamIndex] ) } );
            QCNodeEventInfo_t info( frameDesc, m_nodeId, QC_STATUS_OK,
                                    static_cast<QCObjectState_e>( m_state ) );
            pFrame->id = m_frameId[streamIndex]++;
            m_callback( info );
        }
    }
    else
    {
        QC_ERROR( "Failed to set frame buffer descriptor" );
    }
}

void CameraImpl::EventCallback( const uint32_t eventId, const QCarCamEventPayload_t *pPayLoad )
{
    QCStatus_e ret = QC_STATUS_OK;
    NodeFrameDescriptor frameDesc( 1 );
    QCBufferDescriptorBase_t eventDesc;
    QCarCamEventPayload_t payload;

    if ( QC_OBJECT_STATE_RUNNING != m_state )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Camera is not in running state" );
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( nullptr == m_callback )
        {
            ret = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "callback is invalid" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( nullptr == pPayLoad )
        {
            ret = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "payload is invalid" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        payload = *pPayLoad;
        eventDesc.name = "Camera Event";
        eventDesc.pBuf = &payload;
        eventDesc.size = sizeof( QCarCamEventPayload_t );
        frameDesc.SetBuffer( 0, eventDesc );
        QCNodeEventInfo_t info( frameDesc, m_nodeId, QC_STATUS_FAIL,
                                static_cast<QCObjectState_e>( m_state ) );
        m_callback( info );
        QC_INFO( "Received event: %d, pPayload:%p", eventId, pPayLoad );
    }
    else
    {
        QC_ERROR( "Failed to set event buffer descriptor" );
    }
}

QCarCamRet_e CameraImpl::QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                         const QCarCamEventPayload_t *pPayload,
                                         void *pPrivateData ) noexcept
{
    QCarCamRet_e status = QCARCAM_RET_OK;

    if ( nullptr == pPrivateData )
    {
        status = QCARCAM_RET_FAILED;
        QC_LOG_ERROR( "invalid pPrivateData" );
    }
    else
    {
        CameraImpl *self = (CameraImpl *) pPrivateData;
        status = self->QcarcamEventCb( hndl, eventId, pPayload );
    }

    return status;
}

QCarCamRet_e CameraImpl::QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                         const QCarCamEventPayload_t *pPayload )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCarCamRet_e status = QCARCAM_RET_OK;
    CameraFrameDescriptor_t *pCameraFrame = nullptr;
    uint32_t bufferListId = 0;
    uint32_t bufferIdx = 0;

    QC_DEBUG( "QcarcamEventCb eventId: %u", eventId );

    switch ( eventId )
    {
        case QCARCAM_EVENT_FRAME_READY:
        {
            uint32_t evtBufListId = pPayload->frameInfo.id;
            auto evtIt = m_streamIdToIndexMap.find( evtBufListId );
            if ( evtIt == m_streamIdToIndexMap.end() ||
                 nullptr == m_frameBuffers[evtIt->second].pCamFrameDescs )
            {
                ret = QC_STATUS_OUT_OF_BOUND;
                QC_ERROR( "QcarcamEventCb: FRAME_READY for non-output bufferListId=%u "
                          "(not a registered output stream), skipping",
                          evtBufListId );
            }

            if ( QC_STATUS_OK == ret )
            {
                ret = GetFrame( pPayload->frameInfo, bufferListId, bufferIdx );
            }

            if ( QC_STATUS_OK == ret )
            {
                auto mapIt = m_streamIdToIndexMap.find( bufferListId );
                if ( mapIt != m_streamIdToIndexMap.end() )
                {
                    uint32_t streamIndex = mapIt->second;
                    pCameraFrame = &m_frameBuffers[streamIndex].pCamFrameDescs[bufferIdx];

                    // Drop frames whose buffer is flagged invalid
                    if ( 0U != ( pPayload->frameInfo.flags & QCARCAM_BUFFER_STATUS_INVALID ) )
                    {
                        QC_ERROR( "QcarcamEventCb: invalid buffer received - bufferListId=%u, "
                                  "bufferIdx=%u, flags=0x%x, requestId=%u; dropping frame and "
                                  "returning buffer",
                                  bufferListId, bufferIdx, pPayload->frameInfo.flags,
                                  pPayload->frameInfo.requestId );
                        ReturnInvalidFrame( *pCameraFrame );
                    }
                    else
                    {
                        FrameCallback( pCameraFrame );
                    }
                }
                else
                {
                    QC_ERROR( "QcarcamEventCb: bufferListId %u not found in stream map",
                              bufferListId );
                }
            }
            else
            {
                QC_ERROR( "GetFrame failed for EVENT_FRAME_READY case" );
            }

            break;
        }
        case QCARCAM_EVENT_MULTI_STREAM_FRAME_READY:
        {
            const QCarCamMultiFrameInfo_t &multiFrameInfo = pPayload->multiFrameInfo;
            QCarCamFrameInfo_t frameInfo = {};
            frameInfo.requestId = multiFrameInfo.requestId;
            frameInfo.sofTimestamp = multiFrameInfo.sofTimestamp;
            frameInfo.timestamp = multiFrameInfo.timestamp;
            for ( uint32_t i = 0U; i < multiFrameInfo.numFrameInfo; i++ )
            {
                const QCarCamSingleFrameInfo_t &singleFrameInfo = multiFrameInfo.batchFrameInfo[i];
                frameInfo.id = singleFrameInfo.id;
                frameInfo.flags = singleFrameInfo.flags;
                frameInfo.seqNo = singleFrameInfo.seqNo;
                frameInfo.bufferIndex = singleFrameInfo.bufferIndex;

                auto evtIt = m_streamIdToIndexMap.find( frameInfo.id );
                if ( evtIt == m_streamIdToIndexMap.end() ||
                     nullptr == m_frameBuffers[evtIt->second].pCamFrameDescs )
                {
                    QC_DEBUG( "QcarcamEventCb: MULTI_STREAM_FRAME_READY for non-output "
                              "bufferListId=%u, skipping batch frameIdx %u",
                              frameInfo.id, i );
                    continue;
                }

                ret = GetFrame( frameInfo, bufferListId, bufferIdx );
                if ( QC_STATUS_OK == ret )
                {
                    auto mapIt = m_streamIdToIndexMap.find( bufferListId );
                    if ( mapIt != m_streamIdToIndexMap.end() )
                    {
                        uint32_t streamIndex = mapIt->second;
                        pCameraFrame = &m_frameBuffers[streamIndex].pCamFrameDescs[bufferIdx];

                        // Drop frames whose buffer is flagged invalid
                        if ( 0U != ( singleFrameInfo.flags & QCARCAM_BUFFER_STATUS_INVALID ) )
                        {
                            QC_ERROR( "QcarcamEventCb: invalid buffer received - bufferListId=%u, "
                                      "bufferIdx=%u, flags=0x%x, requestId=%u, batch frameIdx=%u; "
                                      "dropping frame and returning buffer",
                                      bufferListId, bufferIdx, singleFrameInfo.flags,
                                      multiFrameInfo.requestId, i );
                            ReturnInvalidFrame( *pCameraFrame );
                        }
                        else
                        {
                            FrameCallback( pCameraFrame );
                        }
                    }
                    else
                    {
                        QC_ERROR( "QcarcamEventCb: bufferListId %u not found in stream map",
                                  bufferListId );
                    }
                }
                else
                {
                    QC_ERROR(
                            "GetFrame failed for MULTI_STREAM_FRAME_READY case, batch frameIdx: %u",
                            i );
                }
            }
            break;
        }
        case QCARCAM_EVENT_INPUT_SIGNAL:
        {
            QC_ERROR( "QCARCAM received new input signal" );
            break;
        }
        case QCARCAM_EVENT_MC_NOTIFY:
        {
            QC_DEBUG( "received QCARCAM_EVENT_MC_NOTIFY" );
            switch ( pPayload->mcEventInfo.event )
            {
                case QCARCAM_MC_STREAM_CREATE:
                    QC_DEBUG( "MC_STREAM_CREATE, numStreams=%u not implemented",
                              pPayload->mcEventInfo.numStreams );
                    for ( uint32_t j = 0U; j < pPayload->mcEventInfo.numStreams; j++ )
                    {
                        QC_DEBUG( "buffer=%#x", pPayload->mcEventInfo.bufferListId[j] );
                    }
                    break;
                case QCARCAM_MC_STREAM_DESTROY:
                    QC_DEBUG( "MC_STREAM_DESTROY, numStreams=%u not implemented",
                              pPayload->mcEventInfo.numStreams );
                    for ( uint32_t j = 0U; j < pPayload->mcEventInfo.numStreams; j++ )
                    {
                        QC_DEBUG( "buffer=%#x", pPayload->mcEventInfo.bufferListId[j] );
                    }
                    break;
                case QCARCAM_MC_STREAM_START:
                    QC_DEBUG( "MC_STREAM_START, numStreams=%u not implemented",
                              pPayload->mcEventInfo.numStreams );
                    for ( uint32_t j = 0U; j < pPayload->mcEventInfo.numStreams; j++ )
                    {
                        QC_DEBUG( "buffer=%#x", pPayload->mcEventInfo.bufferListId[j] );
                    }
                    break;
                case QCARCAM_MC_STREAM_STOP:
                    QC_DEBUG( "MC_STREAM_STOP, numStreams=%u not implemented",
                              pPayload->mcEventInfo.numStreams );
                    for ( uint32_t j = 0U; j < pPayload->mcEventInfo.numStreams; j++ )
                    {
                        QC_DEBUG( "buffer=%#x", pPayload->mcEventInfo.bufferListId[j] );
                    }
                    break;
                default:
                    QC_ERROR( "event_cb Received unsupported mc event %d",
                              pPayload->mcEventInfo.event );
                    break;
            }
            /* Let the user application to handle the multi-client event */
            EventCallback( eventId, pPayload );
            break;
        }
        case QCARCAM_EVENT_ERROR:
        {
            QC_ERROR( "QCARCAM_EVENT_ERROR: error Id=%d, code=%u, source=%u",
                      pPayload->errInfo.errorId, pPayload->errInfo.errorCode,
                      pPayload->errInfo.errorSource );
            EventCallback( eventId, pPayload );
            break;
        }
        default:
        {
            QC_ERROR( "event_cb Received unsupported event %d", eventId );
            break;
        }
    }

    return status;
}

QCarCamColorFmt_e CameraImpl::GetQcarCamFormat( QCImageFormat_e colorFormat )
{
    QCarCamColorFmt_e qcarcamFormat = QCARCAM_FMT_MAX;

    switch ( colorFormat )
    {
        case QC_IMAGE_FORMAT_RGB888:
        {
            qcarcamFormat = QCARCAM_FMT_RGB_888;
            break;
        }
        case QC_IMAGE_FORMAT_BGR888:
        {
            qcarcamFormat = QCARCAM_FMT_BGR_888;
            break;
        }
        case QC_IMAGE_FORMAT_UYVY:
        {
            qcarcamFormat = QCARCAM_FMT_UYVY_8;
            break;
        }
        case QC_IMAGE_FORMAT_NV12:
        {
            qcarcamFormat = QCARCAM_FMT_NV12;
            break;
        }
        case QC_IMAGE_FORMAT_NV12_UBWC:
        {
            qcarcamFormat = QCARCAM_FMT_UBWC_NV12;
            break;
        }
        case QC_IMAGE_FORMAT_P010:
        {
            qcarcamFormat = QCARCAM_FMT_P010;
            break;
        }
        case QC_IMAGE_FORMAT_TP10_UBWC:
        {
            qcarcamFormat = QCARCAM_FMT_UBWC_TP10;
            break;
        }
        default:
        {
            QC_ERROR( "Unsupport corlor format: %d", colorFormat );
            break;
        }
    }

    return qcarcamFormat;
}

}   // namespace Node
}   // namespace QC
