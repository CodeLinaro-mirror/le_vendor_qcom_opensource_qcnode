// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/sample/SampleCamera.hpp"

#include <fstream>

namespace QC
{
namespace sample
{

static constexpr uint32_t g_kDefaultBufNum = 4U;
static constexpr QCImageFormat_e g_defaultFormat = QC_IMAGE_FORMAT_NV12;


/**
 * @brief Read raw binary data from a file at a specific frame offset.
 *
 * Reads @p size bytes from @p fileName starting at byte offset (frameNum * size).
 * Used in single-file injection mode where all frames are concatenated in one binary file.
 *
 * @param[in]  fileName  Path to the binary file.
 * @param[out] pData     Destination buffer.
 * @param[in]  size      Bytes per frame (also the per-frame stride in the file).
 * @param[in]  frameNum  Zero-based frame index; seek offset is (frameNum * size).
 * @return QC_STATUS_OK on success, QC_STATUS_FAIL on error.
 */
static QCStatus_e ReadRawFrameFromFile( const char *fileName, void *pData, size_t size,
                                        uint32_t frameNum )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( nullptr == fileName || nullptr == pData || 0 == size )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( QC_STATUS_OK == ret )
    {
        std::ifstream file( fileName, std::ios::binary );
        if ( !file.is_open() )
        {
            ret = QC_STATUS_FAIL;
        }
        else
        {
            std::streamoff seekOffset =
                    static_cast<std::streamoff>( frameNum ) * static_cast<std::streamoff>( size );
            file.seekg( seekOffset, std::ios::beg );
            if ( !file.good() )
            {
                ret = QC_STATUS_FAIL;
            }
            else
            {
                file.read( static_cast<char *>( pData ), static_cast<std::streamsize>( size ) );
                ret = ( file.good() || file.eof() ) ? QC_STATUS_OK : QC_STATUS_FAIL;
            }
            file.close();
        }
    }

    return ret;
}

/**
 * @brief Parse a text file containing one file path per line into a vector of strings.
 *
 * Used in multifile injection mode to read the list of per-frame raw data files or
 * per-frame sensor header files. Empty lines and lines with only whitespace are skipped.
 * Windows-style CR characters are stripped automatically.
 *
 * @param[in]  listFileName  Path to the text file containing one file path per line.
 * @param[out] fileList      Output vector populated with the parsed file paths.
 * @return QC_STATUS_OK on success, QC_STATUS_BAD_ARGUMENTS if listFileName is null/empty,
 *         QC_STATUS_FAIL if the file cannot be opened or the resulting list is empty.
 */
static QCStatus_e ReadFileList( const char *listFileName, std::vector<std::string> &fileList )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( nullptr == listFileName || '\0' == listFileName[0] )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        std::ifstream file( listFileName );
        if ( !file.is_open() )
        {
            ret = QC_STATUS_FAIL;
        }
        else
        {
            std::string line;
            while ( std::getline( file, line ) )
            {
                // Strip trailing CR/LF and spaces
                while ( !line.empty() &&
                        ( line.back() == '\r' || line.back() == '\n' || line.back() == ' ' ) )
                {
                    line.pop_back();
                }
                if ( !line.empty() )
                {
                    fileList.push_back( line );
                }
            }
            file.close();

            if ( fileList.empty() )
            {
                ret = QC_STATUS_FAIL;
            }
        }
    }

    return ret;
}

/**
 * @brief Read raw binary data from a specific file in a file list (multifile mode).
 *
 * Selects the file at index (frameIdx % fileList.size()) and reads @p size bytes
 * from the beginning of that file. Used in multifile injection mode where each frame
 * is stored in its own individual file.
 *
 * @param[in]  fileList  Vector of file paths parsed from the file-list text file.
 * @param[out] pData     Destination buffer.
 * @param[in]  size      Number of bytes to read from the selected file.
 * @param[in]  frameIdx  Zero-based frame index; wraps around via modulo fileList.size().
 * @return QC_STATUS_OK on success, QC_STATUS_BAD_ARGUMENTS for empty/null inputs,
 *         QC_STATUS_FAIL if the file cannot be opened or read.
 */
static QCStatus_e ReadRawFrameFromFileList( const std::vector<std::string> &fileList, void *pData,
                                            size_t size, uint32_t frameIdx )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( fileList.empty() || nullptr == pData || 0 == size )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        const std::string &fileName = fileList[frameIdx % fileList.size()];
        ret = ReadRawFrameFromFile( fileName.c_str(), pData, size, 0 );
    }

    return ret;
}

SampleCamera::SampleCamera() : m_camBufferManager( m_logger ) {}
SampleCamera::~SampleCamera() {}

#ifdef QC_ENABLE_HS
std::function<void( const std::uint32_t *, std::size_t )> SampleCamera::GetRunnableCallback()
{
    m_bOrchestratorEnabled = true;
    return std::bind( &SampleCamera::RunnableCallback, this, std::placeholders::_1,
                      std::placeholders::_2 );
}

void SampleCamera::RunnableCallback( const std::uint32_t *rids, std::size_t count )
{
    CameraFrameDescriptor_t camFrameDesc;
    std::unique_lock<std::mutex> lck( m_mutex );

    if ( m_camFrameQueue.empty() )
    {
        (void) m_condVar.wait_for( lck, std::chrono::milliseconds( 1000 ) );
    }

    if ( !m_camFrameQueue.empty() )
    {
        camFrameDesc = m_camFrameQueue.front();
        m_camFrameQueue.pop();
        lck.unlock();
        ProcessFrame( &camFrameDesc );
    }
    else
    {
        QC_ERROR( "camera frame timeout." );
    }
}
#endif

void SampleCamera::ProcessDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    QCStatus_e status = QC_STATUS_OK;
    QCFrameDescriptorNodeIfs &frameDescIfs = eventInfo.frameDesc;
    QCBufferDescriptorBase_t &bufDesc = frameDescIfs.GetBuffer( 0 );

    if ( QC_STATUS_OK == eventInfo.status )
    {
        // frame event
        const CameraFrameDescriptor_t *pCamFrameDesc =
                dynamic_cast<const CameraFrameDescriptor_t *>( &bufDesc );

        if ( nullptr == pCamFrameDesc )
        {
            QC_ERROR( "Frame pointer is empty" );
        }
        else
        {
            if ( false == m_stop )
            {
                std::unique_lock<std::mutex> lck( m_mutex );
                m_camFrameQueue.push( *pCamFrameDesc );
                m_condVar.notify_one();
            }
            else
            {
                uint32_t streamId = pCamFrameDesc->streamId;
                NodeFrameDescriptor frameDesc( 1 );
                (void) frameDesc.SetBuffer( 0, bufDesc );
                m_profilers[streamId].Begin();
                status = m_camera.ProcessFrameDescriptor( frameDesc );
                m_profilers[streamId].End();
                if ( QC_STATUS_OK != status )
                {
                    QC_ERROR( "Failed to process frame descriptor, status=%u", status );
                }
            }
        }
    }
    else
    {
        // error event
        QCarCamEventPayload_t *pEventPayLoad = (QCarCamEventPayload_t *) bufDesc.pBuf;
        if ( pEventPayLoad == nullptr )
        {
            QC_ERROR( "EventPayLoad pointer is empty" );
        }
        else
        {
            uint32_t evtType = pEventPayLoad->u32Data;
            uint32_t errCode = pEventPayLoad->errInfo.errorCode;
            uint32_t inputId = pEventPayLoad->errInfo.inputId;
            uint32_t bufferListId = pEventPayLoad->errInfo.bufferlistId;
            uint32_t frameIdx = pEventPayLoad->errInfo.frameId;
            uint32_t requestId = pEventPayLoad->errInfo.requestId;
            uint64_t timestamp = pEventPayLoad->errInfo.timestamp;
            QC_DEBUG( "Camera Error Event, QCNode status: %u, event type: %u, error code: %u, "
                      "inputId: %u, bufferListId: %u, frameIdx: %u, requestId: %u, "
                      "timestamp: %lu",
                      eventInfo.status, evtType, errCode, inputId, bufferListId, frameIdx,
                      requestId, timestamp );
        }
    }
}

QCStatus_e SampleCamera::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    uint32_t streamNum = Get( config, "number", 1 );
    uint32_t metaDataNum = Get( config, "metadata_number", 0 );
    uint32_t bufferNum = 0;

    m_config.Set<std::string>( "name", m_name );
    m_config.Set<uint32_t>( "id", 0 );
    m_config.Set<uint32_t>( "inputId", Get( config, "input_id", 0 ) );
    m_config.Set<uint32_t>( "srcId", Get( config, "src_id", 0 ) );
    m_config.Set<uint32_t>( "clientId", Get( config, "client_id", 0 ) );
    m_config.Set<uint32_t>( "inputMode", Get( config, "input_mode", 0 ) );
    m_config.Set<uint32_t>( "ispUseCase", Get( config, "isp_use_case", 0 ) );
    m_config.Set<uint32_t>( "camFrameDropPattern", Get( config, "frame_drop_pattern", 0 ) );
    m_config.Set<uint32_t>( "camFrameDropPeriod", Get( config, "frame_drop_period", 0 ) );
    m_config.Set<uint32_t>( "opMode",
                            Get( config, "op_mode", (uint32_t) QCARCAM_OPMODE_OFFLINE_ISP ) );

    m_config.Set<bool>( "requestMode", Get( config, "request_mode", false ) );
    m_config.Set<bool>( "primary", Get( config, "is_primary", false ) );
    m_config.Set<bool>( "recovery", Get( config, "recovery", false ) );
    m_config.Set<bool>( "enableMultiStreamFrameReady",
                        Get( config, "multi_stream_frame_ready", false ) );

    m_bIgnoreError = Get( config, "ignore_error", false );
    m_bImmediateRelease = Get( config, "immediate_release", false );

    m_bEnableMetaData = Get( config, "enable_metadata", false );
    m_config.Set<bool>( "enableMetaData", m_bEnableMetaData );

    // set stream configs
    for ( uint32_t i = 0; i < streamNum; i++ )
    {
        DataTree streamConfig;
        std::vector<uint32_t> bufferIds;
        std::string suffix = "";

        if ( i > 0 )
        {
            suffix = std::to_string( i );
        }

        uint32_t streamId = Get( config, "stream_id" + suffix, i );
        uint32_t contextId = Get( config, "context_id" + suffix, 0U );
        bufferNum = Get( config, "pool_size" + suffix, g_kDefaultBufNum );

        for ( uint32_t j = 0; j < bufferNum; j++ )
        {
            bufferIds.push_back( m_bufferIdx );
            m_bufferIdx++;
        }

        uint32_t width = Get( config, "width" + suffix, 0 );
        if ( 0 == width )
        {
            QC_ERROR( "invalid width for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        uint32_t height = Get( config, "height" + suffix, 0 );
        if ( 0 == height )
        {
            QC_ERROR( "invalid height for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        QCImageFormat_e format = Get( config, "format" + suffix, g_defaultFormat );
        if ( QC_IMAGE_FORMAT_MAX == format )
        {
            QC_ERROR( "invalid format for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        uint32_t submitRequestPattern = Get( config, "submit_request_pattern" + suffix, 0 );
        if ( submitRequestPattern > 10 )
        {
            QC_ERROR( "invalid submitRequestPattern for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        if ( QC_STATUS_OK == ret )
        {
            streamConfig.Set<uint32_t>( "streamId", streamId );
            streamConfig.Set<uint32_t>( "contextId", contextId );
            streamConfig.Set( "bufferIds", bufferIds );
            streamConfig.Set<uint32_t>( "width", width );
            streamConfig.Set<uint32_t>( "height", height );
            streamConfig.SetImageFormat( "format", format );
            streamConfig.Set<uint32_t>( "submitRequestPattern", submitRequestPattern );
            m_streamConfigs.push_back( streamConfig );
        }

        std::string topicName = Get( config, "topic" + suffix, "" );
        if ( "" == topicName )
        {
            QC_ERROR( "no topic for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_topicNameMap[streamId] = topicName;
        }
    }
    m_config.Set( "streamConfigs", m_streamConfigs );

    // set metadata configs
    if ( m_bEnableMetaData )
    {
        for ( uint32_t i = 0; i < metaDataNum; i++ )
        {
            DataTree metaDataConfig;
            std::vector<uint32_t> bufferIds;
            uint32_t bufferListId = 0;
            std::string suffix = "";

            if ( i > 0 )
            {
                suffix = std::to_string( i );
            }

            std::string tag = Get( config, "tag" + suffix, "" );
            bufferNum = Get( config, "buffer_num" + suffix, g_kDefaultBufNum );

            for ( uint32_t j = 0; j < bufferNum; j++ )
            {
                bufferIds.push_back( m_bufferIdx );
                m_bufferIdx++;
            }

            if ( "TUNING_FEATURE_1_MODE" == tag )
            {
                m_bEnableFeature1Mode = true;
                bufferListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_METADATA;
                if ( m_bEnableInjection )
                {
                    bufferListId += 1;
                }

                uint32_t feature1Mode = Get( config, "feature1_mode" + suffix, 0U );
                metaDataConfig.Set<uint32_t>( "Feature1Mode", feature1Mode );
            }
            else if ( "TUNING_FEATURE_2_MODE" == tag )
            {
                m_bEnableFeature2Mode = true;
                bufferListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_METADATA;
                if ( m_bEnableInjection )
                {
                    bufferListId += 1;
                }
                if ( m_bEnableFeature1Mode )
                {
                    bufferListId += 1;
                }

                uint32_t feature2Mode = Get( config, "feature2_mode" + suffix, 0U );
                metaDataConfig.Set<uint32_t>( "Feature2Mode", feature2Mode );
            }
            else if ( "INJECTION_SENSOR_METADATA" == tag )
            {
                m_bEnableInjection = true;
                bufferListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_METADATA;

                ret = ParseInjectionConfig( config );
                if ( QC_STATUS_OK == ret )
                {
                    metaDataConfig.Set( "InjectionConfig", m_injectionCfg );
                }
                else
                {
                    QC_ERROR( "Failed to parse injection config for metadata %u", i );
                    break;
                }
            }
            else
            {
                QC_ERROR( "Invalid metadata tag '%s' for metadata %u", tag.c_str(), i );
                ret = QC_STATUS_BAD_ARGUMENTS;
                break;
            }

            // For INJECTION_SENSOR_METADATA, allocate output metadata buffers (BufferListId=0x300)
            if ( ( "INJECTION_SENSOR_METADATA" == tag ) && ( QC_STATUS_OK == ret ) )
            {
                // Read configurable output metadata buffer count; default to g_kDefaultBufNum (4).
                m_injOutputMetaBufNum =
                        Get( config, "injection_output_meta_buf_num", g_kDefaultBufNum );
                m_injOutputMetaBufListId = (uint32_t) QCARCAM_BUFFERLIST_ID_OUTPUT_METADATA;

                for ( uint32_t j = 0; j < m_injOutputMetaBufNum; j++ )
                {
                    m_injOutputMetaBufIds.push_back( m_bufferIdx );
                    m_bufferIdx++;
                }

                metaDataConfig.Set<uint32_t>( "outputBufferListId", m_injOutputMetaBufListId );
                metaDataConfig.Set( "outputBufferIds", m_injOutputMetaBufIds );
            }

            metaDataConfig.Set<uint32_t>( "bufferListId", bufferListId );
            metaDataConfig.Set( "bufferIds", bufferIds );
            metaDataConfig.Set<std::string>( "tag", tag );
            m_metaDataConfigs.push_back( metaDataConfig );
        }

        if ( QC_STATUS_OK == ret )
        {
            m_config.Set( "metaDataConfigs", m_metaDataConfigs );
        }
    }

    m_dataTree.Set( "static", m_config );

    return ret;
}

QCStatus_e SampleCamera::ParseInjectionConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    m_injFileName = Get( config, "injection_file", std::string( "" ) );
    m_injHeaderFileName = Get( config, "injection_header_file", std::string( "" ) );
    m_injEepromFileName = Get( config, "injection_eeprom_file", std::string( "" ) );
    m_injMultiFile = Get( config, "injection_multifile", false );

    // Output frame dump parameters
    m_injDumpEnable = Get( config, "injection_dump_enable", false );
    m_injDumpPath = Get( config, "injection_dump_path", std::string( "/tmp" ) );
    if ( m_injDumpEnable )
    {
        QC_INFO( "ParseInjectionConfig: output frame dump enabled, path='%s'",
                 m_injDumpPath.c_str() );
    }

    // ISP injection output topic
    m_injOutputTopicName = Get( config, "injection_output_topic", std::string( "" ) );
    if ( !m_injOutputTopicName.empty() )
    {
        QC_INFO( "ParseInjectionConfig: injection output topic='%s'",
                 m_injOutputTopicName.c_str() );
    }

    m_injectionTotalFrames = Get( config, "injection_total_frames", 0U );
    uint32_t injInputBufNum = Get( config, "injection_input_buf_num", g_kDefaultBufNum );

    m_injFrameRate = Get( config, "injection_frame_rate", 30U );
    m_injRepeatNum = Get( config, "injection_repeat_num", 0U );
    m_injInputBufListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_0;

    uint32_t injInputId = Get( config, "injection_input_id", 0U );
    uint32_t injInputMode = Get( config, "injection_input_mode", 0U );
    uint32_t injFeature1Mode = Get( config, "injection_feature1_mode", 0U );
    uint32_t injFeature2Mode = Get( config, "injection_feature2_mode", 0U );
    uint32_t injSceneMode = Get( config, "injection_scene_mode", 0U );
    std::string injFormat = Get( config, "injection_format", std::string( "mipiraw_12" ) );

    uint32_t injWidth = Get( config, "injection_width", 0U );
    uint32_t injHeight = Get( config, "injection_height", 0U );
    uint32_t injStride = Get( config, "injection_stride", 0U );

    // Calculate buffer size from stride * height
    if ( ( injStride > 0 ) && ( injHeight > 0 ) )
    {
        m_injBufSize = injStride * injHeight;
        QC_INFO( "ParseInjectionConfig: injection_buf_size=%u (stride=%u * height=%u)",
                 m_injBufSize, injStride, injHeight );
    }
    else
    {
        m_injBufSize = 0;
    }

    m_injectionCfg.Set<uint32_t>( "inputId", injInputId );
    m_injectionCfg.Set<uint32_t>( "inputBufferListId", m_injInputBufListId );
    m_injectionCfg.Set<uint32_t>( "inputMode", injInputMode );
    m_injectionCfg.Set<uint32_t>( "inputTuningParamFeature1Mode", injFeature1Mode );
    m_injectionCfg.Set<uint32_t>( "inputTuningParamFeature2Mode", injFeature2Mode );
    m_injectionCfg.Set<uint32_t>( "inputSceneMode", injSceneMode );
    m_injectionCfg.Set<uint32_t>( "width", injWidth );
    m_injectionCfg.Set<uint32_t>( "height", injHeight );
    m_injectionCfg.Set<uint32_t>( "stride", injStride );
    m_injectionCfg.Set<std::string>( "format", injFormat );
    m_injectionCfg.Set<std::string>( "fileName", m_injFileName );

    // Allocate raw frame input buffer IDs directly into member variable
    for ( uint32_t i = 0; i < injInputBufNum; i++ )
    {
        m_injInputBufIds.push_back( m_bufferIdx );
        m_bufferIdx++;
    }
    m_injectionCfg.Set( "inputBufferIds", m_injInputBufIds );

    // Sensor header / per-frame metadata buffers
    {
        m_injHeaderBufListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_METADATA + 1U;
        m_injHeaderBufSize = Get( config, "injection_header_buf_size", 0U );
        uint32_t injHeaderBufNum = Get( config, "injection_header_buf_num", g_kDefaultBufNum );

        for ( uint32_t i = 0; i < injHeaderBufNum; i++ )
        {
            m_injHeaderBufIds.push_back( m_bufferIdx );
            m_bufferIdx++;
        }

        m_injectionCfg.Set<uint32_t>( "headerBufferListId", m_injHeaderBufListId );
        m_injectionCfg.Set<uint32_t>( "headerBufferSize", (uint32_t) m_injHeaderBufSize );
        m_injectionCfg.Set<std::string>( "headerFileName", m_injHeaderFileName );
        m_injectionCfg.Set( "headerBufferIds", m_injHeaderBufIds );
    }

    // EEPROM calibration buffers (only if EEPROM file is specified)
    if ( !m_injEepromFileName.empty() )
    {
        m_injEepromBufSize = Get( config, "injection_eeprom_buf_size", 0U );
        if ( 0 == m_injEepromBufSize )
        {
            QC_ERROR( "injection_eeprom_buf_size must be > 0 when injection_eeprom_file is set" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_injEepromBufListId = (uint32_t) QCARCAM_BUFFERLIST_ID_INPUT_METADATA + 2U;
            m_injEepromBufIds.push_back( m_bufferIdx );
            m_bufferIdx++;

            m_injectionCfg.Set<uint32_t>( "eepromBufferListId", m_injEepromBufListId );
            m_injectionCfg.Set<uint32_t>( "eepromBufferSize", (uint32_t) m_injEepromBufSize );
            m_injectionCfg.Set<std::string>( "eepromFileName", m_injEepromFileName );
            m_injectionCfg.Set( "eepromBufferIds", m_injEepromBufIds );
        }
    }

    if ( !m_streamConfigs.empty() )
    {
        m_injStreamId = m_streamConfigs[0].Get<uint32_t>( "streamId", 0 );
        m_injFrameBufIds = m_streamConfigs[0].Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
    }

    // In multifile mode, parse the text file lists for raw frames and sensor headers.
    if ( ( QC_STATUS_OK == ret ) && m_injMultiFile )
    {
        if ( !m_injFileName.empty() )
        {
            QCStatus_e listRet = ReadFileList( m_injFileName.c_str(), m_injFileList );
            if ( QC_STATUS_OK != listRet )
            {
                QC_ERROR( "ParseInjectionConfig: failed to read injection frame file list "
                          "from '%s'",
                          m_injFileName.c_str() );
                ret = listRet;
            }
            else
            {
                QC_INFO( "ParseInjectionConfig: loaded %u injection frame files from '%s'",
                         (uint32_t) m_injFileList.size(), m_injFileName.c_str() );

                if ( 0 == m_injectionTotalFrames )
                {
                    m_injectionTotalFrames = (uint32_t) m_injFileList.size();
                }
            }
        }

        if ( ( QC_STATUS_OK == ret ) && !m_injHeaderFileName.empty() )
        {
            QCStatus_e listRet = ReadFileList( m_injHeaderFileName.c_str(), m_injHeaderFileList );
            if ( QC_STATUS_OK != listRet )
            {
                QC_ERROR( "ParseInjectionConfig: failed to read injection header file list "
                          "from '%s'",
                          m_injHeaderFileName.c_str() );
            }
            else
            {
                QC_INFO( "ParseInjectionConfig: loaded %u injection header files from '%s'",
                         (uint32_t) m_injHeaderFileList.size(), m_injHeaderFileName.c_str() );
            }
        }
    }

    return ret;
}

QCStatus_e SampleCamera::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = SampleIF::Init( name );

    uint32_t streamNum = 0;
    uint32_t metaDataNum = 0;

    if ( QC_STATUS_OK == ret )
    {
        ret = ParseConfig( config );
    }

    if ( QC_STATUS_OK == ret )
    {
        using std::placeholders::_1;
        m_nodeCfg.config = m_dataTree.Dump();
        m_nodeCfg.callback = std::bind( &SampleCamera::ProcessDoneCb, this, _1 );
        QC_INFO( "config: %s", m_nodeCfg.config.c_str() );
    }

    if ( QC_STATUS_OK == ret )
    {
        streamNum = (uint32_t) m_streamConfigs.size();
        metaDataNum = (uint32_t) m_metaDataConfigs.size();

        // Allocate buffer pools for camera stream and metadata.
        uint32_t maxPoolNum = streamNum + metaDataNum + ( m_bEnableInjection ? 4U : 0U );
        m_bufferPools.reserve( maxPoolNum );
        m_bufferPools.resize( streamNum + metaDataNum );

        // Allocate buffers for camera stream
        ret = m_camBufferManager.AllocateFrameBuffers( m_config, m_nodeCfg.buffers, streamNum,
                                                       m_bufferPools, m_nodeId );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Init: Failed to allocate frame buffers" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        // Allocate buffers for camera metadata
        if ( true == m_bEnableMetaData )
        {
            ret = m_camBufferManager.AllocateMetaDataBuffers(
                    m_config, m_nodeCfg.buffers, streamNum, metaDataNum, m_bufferPools, m_nodeId );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Init: Failed to allocate metadata buffers" );
            }
        }
    }

    if ( ( QC_STATUS_OK == ret ) && ( true == m_bEnableMetaData ) )
    {
        // Register vendor tag ops and setup metadata buffers
        ret = m_camBufferManager.PreprocessMetaDataBuffers(
                m_config, m_nodeCfg.buffers, streamNum, metaDataNum, m_bufferPools, m_nodeId,
                m_bEnableInjection ? &m_injSensorMetaTagId : nullptr );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Init: Failed to preprocess metadata buffers" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_camera.Initialize( m_nodeCfg );
    }

    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to Init camera node" );
        if ( m_bIgnoreError )
        {
            ret = QC_STATUS_OK;
            QC_ERROR( "Ignore Initialization Error" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        for ( uint32_t i = 0; i < streamNum; i++ )
        {
            uint32_t streamId = m_streamConfigs[i].Get<uint32_t>( "streamId", UINT32_MAX );
            std::string topicName = m_topicNameMap[streamId];
            std::string streamName = name + ".stream" + std::to_string( streamId );
            m_pubMap[streamId] = std::make_shared<DataPublisher<DataFrames_t>>();
            ret = m_pubMap[streamId]->Init( streamName, topicName );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Create topic %s for stream %u failed: %d", topicName.c_str(), i, ret );
                break;
            }
            else
            {
                m_profilers[streamId].Init( streamName );
            }
        }
    }

    // Initialize ISP injection output topic publisher if configured.
    if ( ( QC_STATUS_OK == ret ) && m_bEnableInjection && !m_injOutputTopicName.empty() )
    {
        m_injOutputPub = std::make_shared<DataPublisher<DataFrames_t>>();
        ret = m_injOutputPub->Init( name + ".isp_output", m_injOutputTopicName );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Create injection output topic %s failed: %d", m_injOutputTopicName.c_str(),
                      ret );
        }
        else
        {
            QC_INFO( "Init: injection output topic '%s' ready", m_injOutputTopicName.c_str() );
        }
    }

    // Perform one-time ISP injection initialization
    if ( ( QC_STATUS_OK == ret ) && m_bEnableInjection )
    {
        ret = InjectionInit();
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Init: InjectionInit failed: %d", ret );
        }
    }

    return ret;
}

QCStatus_e SampleCamera::InjectionInit()
{
    QCStatus_e ret = QC_STATUS_OK;

    // -----------------------------------------------------------------------
    // 1. Cache buffer counts from the vectors populated by ParseInjectionConfig.
    // -----------------------------------------------------------------------
    m_injInputBufNum = (uint32_t) m_injInputBufIds.size();
    m_injHeaderBufNum = (uint32_t) m_injHeaderBufIds.size();
    m_injEepromBufNum = (uint32_t) m_injEepromBufIds.size();
    m_injFrameBufNum = (uint32_t) m_injFrameBufIds.size();

    // -----------------------------------------------------------------------
    // 2. Validate that essential buffer pools are configured.
    // -----------------------------------------------------------------------
    if ( 0 == m_injInputBufNum )
    {
        QC_ERROR( "InjectionInit: no injection input buffers configured" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( 0 == m_injFrameBufNum )
    {
        QC_ERROR( "InjectionInit: no output frame buffers configured" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    // -----------------------------------------------------------------------
    // 3. Compute the per-frame sleep interval from the configured frame rate.
    // -----------------------------------------------------------------------
    if ( QC_STATUS_OK == ret )
    {
        m_injFrameIntervalMs = ( m_injFrameRate > 0 ) ? ( 1000U / m_injFrameRate ) : 33U;
    }

    // -----------------------------------------------------------------------
    // 4. Pre-load the static EEPROM calibration data into its DMA buffer.
    // -----------------------------------------------------------------------
    if ( ( QC_STATUS_OK == ret ) && ( m_injEepromBufNum > 0 ) && ( !m_injEepromFileName.empty() ) &&
         ( m_injEepromBufSize > 0 ) && ( !m_injectionEepromLoaded ) )
    {
        uint32_t globalEepromBufIdx = m_injEepromBufIds[0];
        if ( globalEepromBufIdx < (uint32_t) m_nodeCfg.buffers.size() )
        {
            QCBufferDescriptorBase_t &eepromBufDesc = m_nodeCfg.buffers[globalEepromBufIdx];
            if ( nullptr != eepromBufDesc.pBuf )
            {
                QCStatus_e eepromRet = ReadRawFrameFromFile(
                        m_injEepromFileName.c_str(), eepromBufDesc.pBuf, m_injEepromBufSize, 0 );
                if ( QC_STATUS_OK == eepromRet )
                {
                    m_injectionEepromLoaded = true;
                    QC_INFO( "InjectionInit: EEPROM data loaded from %s (%zu bytes)",
                             m_injEepromFileName.c_str(), m_injEepromBufSize );
                }
                else
                {
                    // Treat as non-fatal: injection can still proceed without EEPROM data.
                    QC_ERROR( "InjectionInit: failed to load EEPROM data from %s",
                              m_injEepromFileName.c_str() );
                }
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        QC_INFO( "InjectionInit: streamId=%u, frameBufNum=%u, "
                 "injInputBufNum=%u, injHeaderBufNum=%u, injEepromBufNum=%u, "
                 "frameRate=%u, totalFrames=%u, repeatNum=%u, frameIntervalMs=%u",
                 m_injStreamId, m_injFrameBufNum, m_injInputBufNum, m_injHeaderBufNum,
                 m_injEepromBufNum, m_injFrameRate, m_injectionTotalFrames, m_injRepeatNum,
                 m_injFrameIntervalMs );
    }

    return ret;
}

QCStatus_e SampleCamera::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = m_camera.Start();

    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to start camera node" );
        if ( m_bIgnoreError )
        {
            QC_ERROR( "Ignore Starting Error" );
            ret = QC_STATUS_OK;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_stop = false;
#ifdef QC_ENABLE_HS
        if ( !m_bOrchestratorEnabled )
        {
#endif
            m_thread = std::thread( &SampleCamera::ThreadMain, this );
#ifdef QC_ENABLE_HS
        }
#endif

        // Start the dump thread (only when dump is enabled). Decouples the
        // QCarCam frame-return path from blocking disk I/O.
        if ( m_injDumpEnable )
        {
            m_dumpStop = false;
            m_dumpThread = std::thread( &SampleCamera::DumpThreadMain, this );
            QC_INFO( "Dump thread started" );
        }

        // Start injection thread if injection is enabled
        if ( m_bEnableInjection )
        {
            m_injectionStop = false;
            m_injectionThread = std::thread( &SampleCamera::InjectionThreadMain, this );
            QC_INFO( "Injection thread started" );
        }
    }

    return ret;
}

void SampleCamera::ProcessFrame( CameraFrameDescriptor_t *pCamFrameDesc )
{
    QCStatus_e ret = QC_STATUS_OK;

    DataFrames_t frames;
    DataFrame_t frame;
    uint32_t streamId = pCamFrameDesc->streamId;

    // Dump ISP output frame if enabled
    if ( m_injDumpEnable && nullptr != pCamFrameDesc->pBuf && pCamFrameDesc->size > 0 )
    {
        uint32_t frameId = (uint32_t) pCamFrameDesc->id;
        std::string dumpFileName;

        auto extractBasename = []( const std::string &path ) -> std::string {
            size_t slash = path.find_last_of( "/\\" );
            return ( slash != std::string::npos ) ? path.substr( slash + 1 ) : path;
        };

        if ( m_injMultiFile && !m_injFileList.empty() )
        {
            // Multifile mode: each frame has a unique input file → unique output name.
            const std::string &inputFile = m_injFileList[frameId % m_injFileList.size()];
            dumpFileName = m_injDumpPath + "/output-" + extractBasename( inputFile );
        }
        else if ( !m_injFileName.empty() )
        {
            // Single-file mode: all frames share one input file → append frame ID.
            std::string baseName = extractBasename( m_injFileName );
            size_t dotPos = baseName.find_last_of( '.' );
            std::string stem =
                    ( dotPos != std::string::npos ) ? baseName.substr( 0, dotPos ) : baseName;
            std::string ext = ( dotPos != std::string::npos ) ? baseName.substr( dotPos ) : "";
            dumpFileName =
                    m_injDumpPath + "/output-" + stem + "_" + std::to_string( frameId ) + ext;
        }

        if ( !dumpFileName.empty() )
        {
            DumpData dumpData;
            dumpData.fileName = std::move( dumpFileName );
            // Copy the frame bytes so the QCarCam buffer can be returned immediately.
            const uint8_t *pBytes = static_cast<const uint8_t *>( pCamFrameDesc->pBuf );
            dumpData.data.assign( pBytes, pBytes + pCamFrameDesc->size );

            std::unique_lock<std::mutex> lck( m_dumpMutex );
            if ( m_dumpQueue.size() >= kMaxDumpNum )
            {
                QC_ERROR( "ProcessFrame: dump queue full (%zu dumpData), dropping frame %u → '%s'",
                          m_dumpQueue.size(), frameId, dumpData.fileName.c_str() );
            }
            else
            {
                m_dumpQueue.push( std::move( dumpData ) );
                m_dumpCondVar.notify_one();
            }
        }
    }

    SharedBuffer_t *pSharedBuffer = new SharedBuffer_t;
    pSharedBuffer->SetBuffer( *pCamFrameDesc );
    pSharedBuffer->pubHandle = (uint64_t) pCamFrameDesc->frameIdx + ( (uint64_t) streamId << 32 );

    std::shared_ptr<SharedBuffer_t> buffer( pSharedBuffer, [&]( SharedBuffer_t *pSharedBuffer ) {
        CameraFrameDescriptor_t camFrameDesc;
        NodeFrameDescriptor frameDesc( 1 );

        uint32_t frameIdx = pSharedBuffer->pubHandle & 0xFFFFFFFFul;
        uint32_t sid = pSharedBuffer->pubHandle >> 32;
        camFrameDesc = pSharedBuffer->imgDesc;
        camFrameDesc.frameIdx = frameIdx;
        camFrameDesc.streamId = sid;

        // In injection mode, return the output frame buffer to qcx through the normal
        // ProcessFrameDescriptor path (the Camera node releases it internally instead of
        // re-submitting), then signal InjectionThreadMain that it can submit the next
        // request. The two steps must be in this order: submit-next must not race the
        // buffer return.
        if ( m_bEnableInjection && sid == m_injStreamId )
        {
            (void) frameDesc.SetBuffer( 0, camFrameDesc );
            m_profilers[sid].Begin();
            m_camera.ProcessFrameDescriptor( frameDesc );
            m_profilers[sid].End();
            {
                std::unique_lock<std::mutex> injLck( m_injectionMutex );
                m_injFrameReadyCount++;
            }
            m_injectionCondVar.notify_one();
            delete pSharedBuffer;
            return;
        }

        // When a common-metadata buffer is active (TUNING_FEATURE sticky modes), submit the
        // returned frame and the metadata as one CameraMetaDataDescriptor: the frame becomes
        // streamRequests[0] and the buffer becomes inputCommonMetadata. Otherwise submit the
        // plain frame descriptor.
        uint32_t commonBufListId = 0;
        uint32_t commonBufIdx = 0;
        QCStatus_e commonRet =
                m_camBufferManager.GetCurrentCommonMetaDataBuffer( commonBufListId, commonBufIdx );
        if ( ( QC_STATUS_OK == commonRet ) && ( 0 != commonBufListId ) )
        {
            CameraMetaDataDescriptor_t metaDesc;
            metaDesc.streamRequestNum = 1;
            metaDesc.streamRequests[0].bufferListId = sid;
            metaDesc.streamRequests[0].bufferIdx = frameIdx;
            metaDesc.inputCommonMetadata.bufferListId = commonBufListId;
            metaDesc.inputCommonMetadata.bufferIdx = commonBufIdx;
            (void) frameDesc.SetBuffer( 0, metaDesc );
        }
        else
        {
            (void) frameDesc.SetBuffer( 0, camFrameDesc );
        }

        uint32_t clientId = m_config.Get<uint32_t>( "clientId", UINT32_MAX );
        bool isPrimary = m_config.Get<bool>( "primary", true );
        if ( true == m_bImmediateRelease )
        {
            /* do nothing as immediate release in the callback */
        }
        else if ( ( 0 != clientId ) && ( false == isPrimary ) )
        {
            m_profilers[sid].Begin();
            m_profilers[sid].End();
        }
        else
        {
            m_profilers[sid].Begin();
            m_camera.ProcessFrameDescriptor( frameDesc );
            m_profilers[sid].End();
        }

        delete pSharedBuffer;
    } );

    frame.frameId = pCamFrameDesc->id;
    frame.buffer = buffer;
    frame.timestamp = pCamFrameDesc->timestamp;
    frames.Add( frame );

    auto it = m_pubMap.find( streamId );
    if ( m_pubMap.end() != it )
    {
        it->second->Publish( frames );
    }
    else
    {
        QC_ERROR( "no publisher for stream %u", streamId );
    }

    // Publish ISP injection output frames to the dedicated injection output topic.
    if ( m_bEnableInjection && m_injOutputPub && ( streamId == m_injStreamId ) )
    {
        m_injOutputPub->Publish( frames );
    }

    if ( true == m_bImmediateRelease )
    {
        uint32_t clientId = m_config.Get<uint32_t>( "clientId", UINT32_MAX );
        bool isPrimary = m_config.Get<bool>( "primary", true );
        if ( ( 0 != clientId ) && ( false == isPrimary ) )
        {
            /* do nothing for multi-client non-primary session */
        }
        else
        {
            CameraFrameDescriptor_t camFrameDesc;
            NodeFrameDescriptor frameDesc( 1 );

            uint32_t frameIdx = pSharedBuffer->pubHandle & 0xFFFFFFFFul;
            uint32_t sid = pSharedBuffer->pubHandle >> 32;
            camFrameDesc = pSharedBuffer->imgDesc;
            camFrameDesc.frameIdx = frameIdx;
            camFrameDesc.streamId = sid;

            // See the deleter above: fold the frame and common metadata into one descriptor.
            uint32_t commonBufListId = 0;
            uint32_t commonBufIdx = 0;
            QCStatus_e commonRet = m_camBufferManager.GetCurrentCommonMetaDataBuffer(
                    commonBufListId, commonBufIdx );
            if ( ( QC_STATUS_OK == commonRet ) && ( 0 != commonBufListId ) )
            {
                CameraMetaDataDescriptor_t metaDesc;
                metaDesc.streamRequestNum = 1;
                metaDesc.streamRequests[0].bufferListId = sid;
                metaDesc.streamRequests[0].bufferIdx = frameIdx;
                metaDesc.inputCommonMetadata.bufferListId = commonBufListId;
                metaDesc.inputCommonMetadata.bufferIdx = commonBufIdx;
                (void) frameDesc.SetBuffer( 0, metaDesc );
            }
            else
            {
                (void) frameDesc.SetBuffer( 0, camFrameDesc );
            }

            m_profilers[streamId].Begin();
            m_camera.ProcessFrameDescriptor( frameDesc );
            m_profilers[streamId].End();
        }
    }
}

QCStatus_e SampleCamera::ProcessMetaDataDescriptor( CameraMetaDataDescriptor_t &metaDesc,
                                                    CameraMetaDataType_e metaDataType )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( m_metaDataNeedsUpdate )
    {
        if ( CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE == metaDataType )
        {
            bool found = false;
            uint32_t val = m_metaDataUpdateValue;
            uint32_t metaDataNum = (uint32_t) m_metaDataConfigs.size();

            for ( uint32_t i = 0; i < metaDataNum; i++ )
            {
                uint32_t bufferListId = m_metaDataConfigs[i].Get<uint32_t>( "bufferListId", 0 );
                if ( bufferListId == metaDesc.inputCommonMetadata.bufferListId )
                {
                    uint32_t newBufferIdx = 0;
                    ret = m_camBufferManager.AdvanceAndUpdateMetaDataBuffer(
                            i, &val, 1, m_nodeCfg.buffers, newBufferIdx );
                    if ( QC_STATUS_OK == ret )
                    {
                        // Update the buffer index in the descriptor with the new ring slot
                        metaDesc.inputCommonMetadata.bufferIdx = newBufferIdx;
                        QC_INFO( "ProcessMetaDataDescriptor: updated metadata, "
                                 "bufferListId: %u, newBufferIdx: %u, value: %u",
                                 bufferListId, newBufferIdx, val );
                    }
                    else
                    {
                        QC_ERROR( "ProcessMetaDataDescriptor: AdvanceAndUpdateMetaDataBuffer "
                                  "failed for config %u",
                                  i );
                    }
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                QC_ERROR( "ProcessMetaDataDescriptor: no matching metadata config for "
                          "bufferListId=%u",
                          metaDesc.inputCommonMetadata.bufferListId );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        NodeFrameDescriptor frameDesc( 1 );
        (void) frameDesc.SetBuffer( 0, metaDesc );
        ret = m_camera.ProcessFrameDescriptor( frameDesc );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "ProcessMetaDataDescriptor: ProcessFrameDescriptor failed, ret=%d", ret );
        }
    }

    return ret;
}

void SampleCamera::ThreadMain()
{
    CameraFrameDescriptor_t camFrameDesc;

    while ( !m_stop )
    {
        std::unique_lock<std::mutex> lck( m_mutex );
        (void) m_condVar.wait_for( lck, std::chrono::milliseconds( 10 ) );

        if ( !m_camFrameQueue.empty() )
        {
            camFrameDesc = m_camFrameQueue.front();
            m_camFrameQueue.pop();
            lck.unlock();
            ProcessFrame( &camFrameDesc );
        }
    }
}


void SampleCamera::InjectionThreadMain()
{
    QCStatus_e ret = QC_STATUS_OK;

    bool injectionCompleted = false;

    uint32_t frameCount = 0;
    uint32_t repeatCount = 0;
    uint32_t injInputNextBufIdx = 0;
    uint32_t injHeaderNextBufIdx = 0;

    while ( !m_injectionStop )
    {
        // -------------------------------------------------------------------
        // 1. Load raw frame data
        // -------------------------------------------------------------------
        uint32_t inputBufIdx = injInputNextBufIdx % m_injInputBufNum;
        injInputNextBufIdx++;
        uint32_t globalInputBufIdx = m_injInputBufIds[inputBufIdx];

        if ( m_injBufSize > 0 && globalInputBufIdx < (uint32_t) m_nodeCfg.buffers.size() )
        {
            QCBufferDescriptorBase_t &bufDesc = m_nodeCfg.buffers[globalInputBufIdx];
            if ( nullptr != bufDesc.pBuf )
            {
                if ( m_injMultiFile )
                {
                    // Multifile mode: each frame is in its own file listed in m_injFileList
                    if ( !m_injFileList.empty() )
                    {
                        ret = ReadRawFrameFromFileList( m_injFileList, bufDesc.pBuf,
                                                        (size_t) m_injBufSize, frameCount );
                        if ( QC_STATUS_OK != ret )
                        {
                            QC_ERROR( "InjectionThreadMain: failed to read raw frame %u "
                                      "from file list",
                                      frameCount );
                        }
                    }
                }
                else if ( !m_injFileName.empty() )
                {
                    // Single-file mode: all frames concatenated in one binary file
                    uint32_t fileFrameNum = ( m_injectionTotalFrames > 0 )
                                                    ? ( frameCount % m_injectionTotalFrames )
                                                    : frameCount;
                    ret = ReadRawFrameFromFile( m_injFileName.c_str(), bufDesc.pBuf,
                                                (size_t) m_injBufSize, fileFrameNum );
                    if ( QC_STATUS_OK != ret )
                    {
                        QC_ERROR( "InjectionThreadMain: failed to read raw frame %u from %s",
                                  fileFrameNum, m_injFileName.c_str() );
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        // 2. Load sensor header / per-frame metadata
        // -------------------------------------------------------------------
        uint32_t headerBufIdx = 0;
        if ( m_injHeaderBufNum > 0 )
        {
            headerBufIdx = injHeaderNextBufIdx % m_injHeaderBufNum;
            injHeaderNextBufIdx++;
            uint32_t globalHeaderBufIdx = m_injHeaderBufIds[headerBufIdx];

            if ( m_injHeaderBufSize > 0 &&
                 globalHeaderBufIdx < (uint32_t) m_nodeCfg.buffers.size() )
            {
                QCBufferDescriptorBase_t &headerBufDesc = m_nodeCfg.buffers[globalHeaderBufIdx];
                if ( nullptr != headerBufDesc.pBuf )
                {
                    // Read raw sensor metadata blob into a temporary buffer
                    std::vector<uint8_t> sensorMetaData( m_injHeaderBufSize, 0 );
                    QCStatus_e headerRet = QC_STATUS_FAIL;

                    if ( m_injMultiFile )
                    {
                        if ( !m_injHeaderFileList.empty() )
                        {
                            headerRet = ReadRawFrameFromFileList( m_injHeaderFileList,
                                                                  sensorMetaData.data(),
                                                                  m_injHeaderBufSize, frameCount );
                            if ( QC_STATUS_OK != headerRet )
                            {
                                QC_ERROR( "InjectionThreadMain: failed to read header frame %u "
                                          "from file list",
                                          frameCount );
                            }
                        }
                    }
                    else if ( !m_injHeaderFileName.empty() )
                    {
                        uint32_t fileFrameNum = ( m_injectionTotalFrames > 0 )
                                                        ? ( frameCount % m_injectionTotalFrames )
                                                        : frameCount;
                        headerRet = ReadRawFrameFromFile( m_injHeaderFileName.c_str(),
                                                          sensorMetaData.data(), m_injHeaderBufSize,
                                                          fileFrameNum );
                        if ( QC_STATUS_OK != headerRet )
                        {
                            QC_ERROR( "InjectionThreadMain: failed to read header frame %u "
                                      "from %s",
                                      fileFrameNum, m_injHeaderFileName.c_str() );
                        }
                    }

                    if ( QC_STATUS_OK == headerRet )
                    {
                        // Update the INJECTION_SENSOR_METADATA entry in the
                        // pre-initialized camera_metadata_t buffer with the actual
                        // sensor metadata blob read from the header file.
                        camera_metadata_t *pMetaData = (camera_metadata_t *) headerBufDesc.pBuf;
                        camera_metadata_entry_t entry;
                        if ( 0 ==
                             find_camera_metadata_entry( pMetaData, m_injSensorMetaTagId, &entry ) )
                        {
                            int rc = update_camera_metadata_entry( pMetaData, entry.index,
                                                                   sensorMetaData.data(),
                                                                   m_injHeaderBufSize, nullptr );
                            if ( 0 != rc )
                            {
                                QC_ERROR( "InjectionThreadMain: update_camera_metadata_entry "
                                          "failed for INJECTION_SENSOR_METADATA, "
                                          "frame %u, rc=%d",
                                          frameCount, rc );
                            }
                        }
                        else
                        {
                            QC_ERROR( "InjectionThreadMain: INJECTION_SENSOR_METADATA entry "
                                      "not found in header buffer %u (tagId=%u)",
                                      headerBufIdx, m_injSensorMetaTagId );
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        // 3. Select EEPROM buffer (already loaded; always use buffer index 0)
        // -------------------------------------------------------------------
        uint32_t eepromBufIdx = 0;

        // -------------------------------------------------------------------
        // 4. Get the next output frame buffer index (ring buffer)
        // -------------------------------------------------------------------
        uint32_t frameBufIdx = frameCount % m_injFrameBufNum;

        // -------------------------------------------------------------------
        // 5. Build and submit the injection request
        // -------------------------------------------------------------------
        CameraMetaDataDescriptor_t metaDesc;
        metaDesc.flags = QCARCAM_REQUEST_FLAG_INJECTION;
        metaDesc.requestId = frameCount;
        metaDesc.streamRequestNum = 1;
        metaDesc.syncId = 0;

        // Raw frame data → inputBuffer
        metaDesc.inputBuffer.bufferListId = m_injInputBufListId;
        metaDesc.inputBuffer.bufferIdx = inputBufIdx;

        // Sensor header → inputCommonMetadata
        metaDesc.inputCommonMetadata.bufferListId = m_injHeaderBufListId;
        metaDesc.inputCommonMetadata.bufferIdx = headerBufIdx;

        // EEPROM calibration → inputMetadata[0]
        if ( m_injEepromBufNum > 0 && m_injectionEepromLoaded )
        {
            metaDesc.inputMetadata[0].bufferListId = m_injEepromBufListId;
            metaDesc.inputMetadata[0].bufferIdx = eepromBufIdx;
        }

        // Output frame buffer
        metaDesc.streamRequests[0].bufferListId = m_injStreamId;
        metaDesc.streamRequests[0].bufferIdx = frameBufIdx;

        ret = ProcessMetaDataDescriptor( metaDesc, CAMERA_METADATA_TYPE_ISP_INJECTION );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "InjectionThreadMain: failed to submit injection request %u, ret=%d",
                      frameCount, ret );
        }
        else
        {
            QC_DEBUG( "InjectionThreadMain: submitted injection request %u, "
                      "inputBufIdx=%u, headerBufIdx=%u, eepromBufIdx=%u, frameBufIdx=%u",
                      frameCount, inputBufIdx, headerBufIdx, eepromBufIdx, frameBufIdx );
        }

        frameCount++;

        // Check if we've reached the total frame count
        if ( m_injectionTotalFrames > 0 && frameCount >= m_injectionTotalFrames )
        {
            repeatCount++;
            if ( m_injRepeatNum > 0 && repeatCount >= m_injRepeatNum )
            {
                // All repetitions done — mark as completed and exit the loop automatically.
                injectionCompleted = true;
                break;
            }
            frameCount = 0;
        }

        // Wait for FRAME_READY on the injection output stream before submitting the next
        // request
        std::unique_lock<std::mutex> lck( m_injectionMutex );
        m_injectionCondVar.wait( lck, [this, frameCount] {
            return m_injectionStop || m_injFrameReadyCount >= frameCount;
        } );

        if ( m_injectionStop )
        {
            break;
        }
    }

    if ( injectionCompleted )
    {
        QC_INFO( "ISP injection completed: %u frame(s) x %u repetition(s) injected successfully",
                 m_injectionTotalFrames, m_injRepeatNum );
    }
    QC_INFO( "InjectionThreadMain: exiting after %u frames", frameCount );
}

void SampleCamera::DumpThreadMain()
{
    while ( true )
    {
        DumpData dumpData;

        std::unique_lock<std::mutex> lck( m_dumpMutex );
        bool ready = m_dumpCondVar.wait_for( lck, std::chrono::milliseconds( 50 ), [this] {
            return m_dumpStop || !m_dumpQueue.empty();
        } );
        if ( m_dumpStop && m_dumpQueue.empty() )
        {
            break;
        }
        if ( !ready )
        {
            break;
        }
        dumpData = std::move( m_dumpQueue.front() );
        m_dumpQueue.pop();

        std::ofstream dumpFile( dumpData.fileName, std::ios::binary );
        if ( dumpFile.is_open() )
        {
            dumpFile.write( reinterpret_cast<const char *>( dumpData.data.data() ),
                            static_cast<std::streamsize>( dumpData.data.size() ) );
            dumpFile.close();
            QC_INFO( "DumpThreadMain: wrote '%s' (%zu bytes)", dumpData.fileName.c_str(),
                     dumpData.data.size() );
        }
        else
        {
            QC_ERROR( "DumpThreadMain: failed to open dump file '%s'", dumpData.fileName.c_str() );
        }
    }
    QC_INFO( "DumpThreadMain: exiting" );
}

QCStatus_e SampleCamera::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;

    // Stop injection thread first
    if ( m_bEnableInjection )
    {
        std::unique_lock<std::mutex> lck( m_injectionMutex );
        m_injectionStop = true;
        m_injectionCondVar.notify_all();
        if ( m_injectionThread.joinable() )
        {
            m_injectionThread.join();
        }
        QC_INFO( "Injection thread stopped" );
    }

    m_stop = true;
    m_condVar.notify_all();
#ifdef QC_ENABLE_HS
    if ( !m_bOrchestratorEnabled )
    {
#endif
        if ( m_thread.joinable() )
        {
            m_thread.join();
        }
#ifdef QC_ENABLE_HS
    }
#endif

    CameraFrameDescriptor_t camFrameDesc;
    while ( !m_camFrameQueue.empty() )
    {
        std::unique_lock<std::mutex> lck( m_mutex );
        camFrameDesc = m_camFrameQueue.front();
        m_camFrameQueue.pop();

        ProcessFrame( &camFrameDesc );
    }

    if ( m_dumpThread.joinable() )
    {
        std::unique_lock<std::mutex> lck( m_dumpMutex );
        m_dumpStop = true;

        m_dumpCondVar.notify_all();
        m_dumpThread.join();
        QC_INFO( "Dump thread stopped" );
    }

    for ( auto &it : m_pubMap )
    {
        it.second->Clear();
    }

    // Clear the ISP injection output topic publisher if it was initialized.
    if ( m_injOutputPub )
    {
        m_injOutputPub->Clear();
    }

    ret = m_camera.Stop();

    return ret;
}

QCStatus_e SampleCamera::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = m_camera.DeInitialize();

    return ret;
}

const uint32_t SampleCamera::GetVersion() const
{
    return QCNODE_CAMERA_VERSION;
}

REGISTER_SAMPLE( Camera, SampleCamera );

}   // namespace sample
}   // namespace QC
