// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "CameraBufferManager.hpp"

#include <unordered_map>

// ---------------------------------------------------------------------------
// Vendor tag ops registration
//
// set_camera_metadata_vendor_ops() (camera_metadata_hidden.h) registers a
// vendor_tag_ops_t with the camera_metadata library so that vendor-defined tags
// (e.g. INJECTION_SENSOR_METADATA = 0x806c0000) are recognized by
// add_camera_metadata_entry / update_camera_metadata_entry.
// This matches qcarcam_test behavior:
//   QCarCamMetadataGetVendorOps(&pMainVendorTagOps);
//   set_camera_metadata_vendor_ops(&pMainVendorTagOps);
// ---------------------------------------------------------------------------

/** Module-level copy of the vendor_tag_ops_t filled by QCarCamMetadataGetVendorOps. */
static vendor_tag_ops_t s_vendorTagOps = {};

// Forward declaration for set_camera_metadata_vendor_ops from camera_metadata_hidden.h.
// This function properly registers vendor_tag_ops_t with the camera_metadata library.
extern "C" int set_camera_metadata_vendor_ops( const vendor_tag_ops_t *ops );

namespace QC
{
namespace sample
{

/** @brief Maximum number of metadata tags per camera_metadata buffer */
static constexpr uint32_t kMaxMetadataTagNum = 50U;

/** @brief Maximum data size per metadata tag in bytes */
static constexpr uint32_t kMaxMetadataTagData = 65536U;

/**
 * @brief Automatically calculate the camera_metadata buffer size for a given tag.
 *
 * Uses calculate_camera_metadata_size() from camera_metadata.h to compute the
 * exact buffer size required to hold the camera_metadata structure with
 * appropriate entry and data capacity for the given tag type.
 *
 * Calculation strategy per tag:
 *
 *   TUNING_FEATURE_1_MODE / TUNING_FEATURE_2_MODE:
 *     A single uint32_t (TYPE_INT32, count=1) entry.
 *     calculate_camera_metadata_entry_data_size(TYPE_INT32, 1) gives the extra
 *     data bytes needed (0 for inline storage), then
 *     calculate_camera_metadata_size(1, dataSize) gives the total buffer size.
 *
 *   INJECTION_SENSOR_METADATA / generic tags:
 *     The number of entries and data size are not known in advance, so
 *     kMaxMetadataTagNum entries and kMaxMetadataTagData bytes are used as
 *     safe upper bounds:
 *     calculate_camera_metadata_size(kMaxMetadataTagNum, kMaxMetadataTagData).
 *
 * @param[in] tag  The metadata tag name string.
 * @return Required buffer size in bytes.
 */
static size_t CalculateMetaDataBufferSize( const std::string &tag )
{
    size_t bufferSize = 0;
    if ( ( "TUNING_FEATURE_1_MODE" == tag ) || ( "TUNING_FEATURE_2_MODE" == tag ) )
    {
        // Single uint32_t entry; TYPE_INT32 with count=1 typically fits inline
        // (data_count == 0), so the buffer only needs space for the header + 1 entry slot.
        size_t dataSize = calculate_camera_metadata_entry_data_size( TYPE_INT32, 1 );
        bufferSize = calculate_camera_metadata_size( 1, dataSize );
    }
    else
    {
        // INJECTION_SENSOR_METADATA and all other tags: use kMaxMetadataTagNum /
        // kMaxMetadataTagData as safe upper bounds so the buffer can accommodate any vendor tag
        // payload.
        bufferSize = calculate_camera_metadata_size( kMaxMetadataTagNum, kMaxMetadataTagData );
    }

    return bufferSize;
}

/**
 * @brief Static lookup table mapping tag name strings to QCarCamMetadataTagId_e values.
 */
static const std::unordered_map<std::string, QCarCamMetadataTagId_e> s_tagNameToQccIdMap = {
        { "INJECTION_SENSOR_METADATA", QCARCAM_METADATA_TAG_INJECTION_SENSOR_METADATA },
        { "SATURATION_LEVEL", QCARCAM_METADATA_TAG_SATURATION_LEVEL },
        { "CONTRAST_LEVEL", QCARCAM_METADATA_TAG_CONTRAST_LEVEL },
        { "SHARPNESS_STRENGTH", QCARCAM_METADATA_TAG_SHARPNESS_STRENGTH },
        { "ICA_LDC_TRANSFORM_MODE", QCARCAM_METADATA_TAG_ICA_LDC_TRANSFORM_MODE },
        { "ICA_IN_GRID_OUT_2_IN_TRANSFORM", QCARCAM_METADATA_TAG_ICA_IN_GRID_OUT_2_IN_TRANSFORM },
        { "CAM_SETTINGS", QCARCAM_METADATA_TAG_CAM_SETTINGS },
        { "OVERRIDE_STATE", QCARCAM_METADATA_TAG_OVERRIDE_STATE },
        { "AEC_CUSTOM_DEBUG_DATA", QCARCAM_METADATA_TAG_AEC_CUSTOM_DEBUG_DATA },
        { "AEC_BHIST_METADATA", QCARCAM_METADATA_TAG_AEC_BHIST_METADATA },
        { "TUNING_FEATURE_1_MODE", QCARCAM_METADATA_TAG_TUNING_FEATURE_1_MODE },
        { "TUNING_FEATURE_2_MODE", QCARCAM_METADATA_TAG_TUNING_FEATURE_2_MODE },
        { "SENSOR_IN_BYPASS", QCARCAM_METADATA_TAG_SENSOR_IN_BYPASS },
        { "AEC_LUX_INDEX", QCARCAM_METADATA_TAG_AEC_LUX_INDEX },
        { "AEC_LUMINANCE_DATA", QCARCAM_METADATA_TAG_AEC_LUMINANCE_DATA },
        { "DEBUG_IMAGE_DUMP", QCARCAM_METADATA_TAG_DEBUG_IMAGE_DUMP },
        { "AEC_COMPEN_ADRC_GAIN", QCARCAM_METADATA_TAG_AEC_COMPEN_ADRC_GAIN },
        { "AEC_COMPEN_DARK_BOOST_GAIN", QCARCAM_METADATA_TAG_AEC_COMPEN_DARK_BOOST_GAIN },
        { "DIGITAL_GAIN_CONTROL", QCARCAM_METADATA_TAG_DIGITAL_GAIN_CONTROL },
        { "AEC_EXPOSURE_TIME", QCARCAM_METADATA_TAG_AEC_EXPOSURE_TIME },
        { "AEC_LINEAR_GAIN", QCARCAM_METADATA_TAG_AEC_LINEAR_GAIN },
        { "AWB_FRAME_CONTROL_R_GAIN", QCARCAM_METADATA_TAG_AWB_FRAME_CONTROL_R_GAIN },
        { "AWB_FRAME_CONTROL_B_GAIN", QCARCAM_METADATA_TAG_AWB_FRAME_CONTROL_B_GAIN },
        { "AWB_FRAME_CONTROL_G_GAIN", QCARCAM_METADATA_TAG_AWB_FRAME_CONTROL_G_GAIN },
        { "AWB_FRAME_CONTROL_CCT", QCARCAM_METADATA_TAG_AWB_FRAME_CONTROL_CCT },
        { "ICA_CAMERA_MATRIX", QCARCAM_METADATA_TAG_ICA_CAMERA_MATRIX },
};

CameraBufferManager::CameraBufferManager( Logger &logger ) : m_logger( logger ) {}
CameraBufferManager::~CameraBufferManager() {}

QCStatus_e CameraBufferManager::AllocateFrameBuffers(
        DataTree &configTree,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers, uint32_t streamNum,
        std::vector<SharedBufferPool> &bufferPools, QCNodeID_t &nodeId )
{
    QCStatus_e ret = QC_STATUS_OK;

    uint32_t streamId = 0;
    ImageProps_t imgProp;
    std::string name = configTree.Get<std::string>( "name", "" );

    std::vector<DataTree> streamConfigs;
    ret = configTree.Get( "streamConfigs", streamConfigs );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "AllocateFrameBuffers: failed to get streamConfigs" );
    }

    if ( ( QC_STATUS_OK == ret ) && ( bufferPools.size() < streamNum ) )
    {
        QC_ERROR( "AllocateFrameBuffers: buffer pool size %u < streamNum %u",
                  (uint32_t) bufferPools.size(), streamNum );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    for ( size_t i = 0; ( QC_STATUS_OK == ret ) && ( i < streamNum ); i++ )
    {
        DataTree &streamCfg = streamConfigs[i];
        streamId = streamCfg.Get<uint32_t>( "streamId", UINT32_MAX );
        std::vector<uint32_t> bufferIds =
                streamCfg.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        uint32_t bufferNum = (uint32_t) bufferIds.size();

        imgProp.format = streamCfg.GetImageFormat( "format", QC_IMAGE_FORMAT_MAX );
        imgProp.width = streamCfg.Get<uint32_t>( "width", UINT32_MAX );
        imgProp.height = streamCfg.Get<uint32_t>( "height", UINT32_MAX );

        if ( ( QC_IMAGE_FORMAT_MAX == imgProp.format ) || ( UINT32_MAX == imgProp.width ) ||
             ( UINT32_MAX == imgProp.height ) )
        {
            QC_ERROR( "AllocateFrameBuffers: invalid stream config for stream %u", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
            break;
        }

        std::string bufPoolName = name + "_stream_" + std::to_string( i );

        if ( ( QC_IMAGE_FORMAT_RGB888 == imgProp.format ) ||
             ( QC_IMAGE_FORMAT_BGR888 == imgProp.format ) )
        {
            imgProp.batchSize = 1;
            imgProp.stride[0] = QC_ALIGN_SIZE( imgProp.width * 3, 16 );
            imgProp.actualHeight[0] = imgProp.height;
            imgProp.numPlanes = 1;
            imgProp.planeBufSize[0] = 0;
            imgProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
            imgProp.cache = QC_CACHEABLE;

            ret = bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                       imgProp );
        }
        else
        {
            ret = bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                       imgProp.width, imgProp.height, imgProp.format,
                                       QC_MEMORY_ALLOCATOR_DMA_CAMERA, QC_CACHEABLE );
        }

        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "AllocateFrameBuffers: Failed to init buffer pool for stream %u "
                      "(streamId=%u)",
                      i, streamId );
            break;
        }

        ret = bufferPools[i].GetBuffers( buffers );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "AllocateFrameBuffers: Failed to get buffers for stream %u "
                      "(streamId=%u)",
                      i, streamId );
            break;
        }

        QC_INFO( "AllocateFrameBuffers: allocated %u buffers for stream %u (streamId=%u), "
                 "format=%d, %ux%u",
                 bufferNum, i, streamId, (int) imgProp.format, imgProp.width, imgProp.height );
    }

    return ret;
}

QCStatus_e CameraBufferManager::AllocateMetaDataBuffers(
        DataTree &configTree,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers, uint32_t streamNum,
        uint32_t metaDataNum, std::vector<SharedBufferPool> &bufferPools, QCNodeID_t &nodeId )
{
    QCStatus_e ret = QC_STATUS_OK;

    uint32_t bufferNum = 0;
    uint32_t bufferSize = 0;
    uint32_t bufferListId = 0;
    uint32_t bufferListType = 0;
    BufferProps_t bufferProp;
    std::string tag = "";
    std::string name = configTree.Get<std::string>( "name", "" );

    std::vector<DataTree> metaDataConfigs;
    ret = configTree.Get( "metaDataConfigs", metaDataConfigs );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "AllocateMetaDataBuffers: failed to get metaDataConfigs" );
    }

    if ( ( QC_STATUS_OK == ret ) && ( bufferPools.size() < ( streamNum + metaDataNum ) ) )
    {
        QC_ERROR( "AllocateMetaDataBuffers: buffer pool size %u < streamNum+metaDataNum %u",
                  (uint32_t) bufferPools.size(), streamNum + metaDataNum );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    for ( size_t i = 0; ( QC_STATUS_OK == ret ) && ( i < metaDataNum ); i++ )
    {
        size_t poolIdx = streamNum + i;
        DataTree &metaCfg = metaDataConfigs[i];
        tag = metaCfg.Get<std::string>( "tag", "" );
        bufferListId = metaCfg.Get<uint32_t>( "bufferListId", 0 );
        std::vector<uint32_t> bufferIds =
                metaCfg.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        bufferNum = (uint32_t) bufferIds.size();
        bufferListType = QCARCAM_GET_BUFFERLIST_TYPE( bufferListId );

        // Determine buffer size for this metadata config.
        // If the user has not specified a bufferSize (== 0), automatically calculate the
        // required size using calculate_camera_metadata_size() via
        // CalculateMetaDataBufferSize().  This ensures the buffer is always large enough
        // to hold the camera_metadata structure without requiring the user to guess a value.
        bufferSize = metaCfg.Get<uint32_t>( "bufferSize", 0 );
        if ( 0 == bufferSize )
        {
            bufferSize = (uint32_t) CalculateMetaDataBufferSize( tag );
            QC_INFO( "AllocateMetaDataBuffers: auto-calculated bufferSize=%u for "
                     "%s metadata %u (via calculate_camera_metadata_size)",
                     bufferSize, tag.c_str(), (uint32_t) i );
        }

        if ( 0 == bufferNum )
        {
            QC_ERROR( "AllocateMetaDataBuffers: bufferIds is empty for metadata %u (tag=%s)", i,
                      tag.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
            break;
        }

        bufferProp.size = bufferSize;
        bufferProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
        bufferProp.cache = QC_CACHEABLE;

        std::string bufPoolName = name + "_metadata_" + std::to_string( i ) + "_" + tag;
        ret = bufferPools[poolIdx].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufferNum,
                                         bufferProp );

        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "AllocateMetaDataBuffers: Failed to init buffer pool for metadata %u "
                      "(tag=%s)",
                      i, tag.c_str() );
            break;
        }

        ret = bufferPools[poolIdx].GetBuffers( buffers );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "AllocateMetaDataBuffers: Failed to get buffers for metadata %u "
                      "(tag=%s)",
                      i, tag.c_str() );
            break;
        }

        QC_INFO( "AllocateMetaDataBuffers: allocated %u metadata buffers for metadata %u "
                 "(tag=%s, bufferSize=%u, bufferListId=0x%x)",
                 bufferNum, i, tag.c_str(), bufferSize, bufferListId );

        // For INJECTION_SENSOR_METADATA: also allocate injection input, header, and EEPROM buffers
        if ( ( QC_STATUS_OK == ret ) && ( "INJECTION_SENSOR_METADATA" == tag ) )
        {
            DataTree injCfgDt;
            if ( QC_STATUS_OK == metaCfg.Get( "InjectionConfig", injCfgDt ) )
            {
                // Helper lambda: allocate a buffer pool and register it
                auto AllocInjPool = [&]( const char *label, const std::vector<uint32_t> &bufIds,
                                         uint32_t bSize ) -> QCStatus_e {
                    QCStatus_e localRet = QC_STATUS_OK;
                    uint32_t bNum = (uint32_t) bufIds.size();

                    if ( 0 == bNum )
                    {
                        QC_DEBUG( "AllocateMetaDataBuffers: no %s buffers for metadata %u", label,
                                  i );
                    }
                    else if ( 0 == bSize )
                    {
                        QC_ERROR( "AllocateMetaDataBuffers: %s bufferSize is 0 for "
                                  "metadata %u, cannot allocate",
                                  label, i );
                        localRet = QC_STATUS_BAD_ARGUMENTS;
                    }
                    else
                    {
                        BufferProps_t bProp;
                        bProp.size = bSize;
                        bProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
                        bProp.cache = QC_CACHEABLE;

                        bufferPools.emplace_back();
                        size_t poolIdx = bufferPools.size() - 1;

                        std::string poolName = name + "_inj_" + label + "_" + std::to_string( i );
                        localRet = bufferPools[poolIdx].Init( poolName, nodeId, LOGGER_LEVEL_ERROR,
                                                              bNum, bProp );
                        if ( QC_STATUS_OK != localRet )
                        {
                            QC_ERROR( "AllocateMetaDataBuffers: Failed to init %s buffer "
                                      "pool for metadata %u",
                                      label, i );
                        }
                        else
                        {
                            localRet = bufferPools[poolIdx].GetBuffers( buffers );
                            if ( QC_STATUS_OK != localRet )
                            {
                                QC_ERROR( "AllocateMetaDataBuffers: Failed to get %s "
                                          "buffers for metadata %u",
                                          label, i );
                            }
                            else
                            {
                                QC_INFO( "AllocateMetaDataBuffers: allocated %u %s buffers "
                                         "for metadata %u (bufferSize=%u)",
                                         bNum, label, i, bSize );
                            }
                        }
                    }
                    return localRet;
                };

                // 1. Raw frame input buffers (QCARCAM_BUFFERLIST_TYPE_INPUT)
                {
                    std::vector<uint32_t> ids =
                            injCfgDt.Get<uint32_t>( "inputBufferIds", std::vector<uint32_t>{} );
                    uint32_t sz = injCfgDt.Get<uint32_t>( "bufferSize", 0 );
                    if ( 0 == sz )
                    {
                        uint32_t stride = injCfgDt.Get<uint32_t>( "stride", 0 );
                        uint32_t height = injCfgDt.Get<uint32_t>( "height", 0 );
                        if ( ( stride > 0 ) && ( height > 0 ) )
                        {
                            sz = stride * height;
                            QC_INFO( "AllocateMetaDataBuffers: auto-calculated injection "
                                     "input bufferSize=%u (stride=%u * height=%u) for "
                                     "metadata %u",
                                     sz, stride, height, (uint32_t) i );
                        }
                    }
                    ret = AllocInjPool( "input", ids, sz );
                }

                // 2. Sensor header / per-frame metadata buffers (INPUT_METADATA)
                if ( QC_STATUS_OK == ret )
                {
                    std::vector<uint32_t> ids =
                            injCfgDt.Get<uint32_t>( "headerBufferIds", std::vector<uint32_t>{} );
                    if ( ids.size() > 0 )
                    {
                        uint32_t sz = (uint32_t) CalculateMetaDataBufferSize(
                                "INJECTION_SENSOR_METADATA" );
                        ret = AllocInjPool( "header", ids, sz );
                    }
                }

                // 3. EEPROM calibration buffers (INPUT_METADATA, separate list)
                if ( QC_STATUS_OK == ret )
                {
                    std::vector<uint32_t> ids =
                            injCfgDt.Get<uint32_t>( "eepromBufferIds", std::vector<uint32_t>{} );
                    uint32_t sz = injCfgDt.Get<uint32_t>( "eepromBufferSize", 0 );
                    if ( ids.size() > 0 )
                    {
                        ret = AllocInjPool( "eeprom", ids, sz );
                    }
                }
            }
        }

        // Allocate output metadata buffers if outputBufferListId is set.
        // These are OUTPUT_METADATA type buffers that the qcx server writes
        // AEC/AWB/ISP result metadata into per frame (e.g. for INJECTION_SENSOR_METADATA).
        // Buffer size is auto-calculated the same way as the input metadata buffer.
        if ( QC_STATUS_OK == ret )
        {
            uint32_t outputBufListId = metaCfg.Get<uint32_t>( "outputBufferListId", 0 );
            if ( 0 != outputBufListId )
            {
                std::vector<uint32_t> outputBufIds =
                        metaCfg.Get<uint32_t>( "outputBufferIds", std::vector<uint32_t>{} );
                uint32_t outputBufNum = (uint32_t) outputBufIds.size();

                if ( outputBufNum > 0 )
                {
                    // Use the same auto-calculated size as the input metadata buffer
                    uint32_t outputBufSize = (uint32_t) CalculateMetaDataBufferSize( tag );

                    BufferProps_t outputBufProp;
                    outputBufProp.size = outputBufSize;
                    outputBufProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_CAMERA;
                    outputBufProp.cache = QC_CACHEABLE;

                    bufferPools.emplace_back();
                    size_t outputPoolIdx = bufferPools.size() - 1;

                    std::string outputPoolName =
                            name + "_output_metadata_" + std::to_string( i ) + "_" + tag;
                    ret = bufferPools[outputPoolIdx].Init( outputPoolName, nodeId,
                                                           LOGGER_LEVEL_ERROR, outputBufNum,
                                                           outputBufProp );
                    if ( QC_STATUS_OK != ret )
                    {
                        QC_ERROR( "AllocateMetaDataBuffers: Failed to init output metadata "
                                  "buffer pool for metadata %u (bufferListId=0x%x)",
                                  (uint32_t) i, outputBufListId );
                    }
                    else
                    {
                        ret = bufferPools[outputPoolIdx].GetBuffers( buffers );
                        if ( QC_STATUS_OK != ret )
                        {
                            QC_ERROR( "AllocateMetaDataBuffers: Failed to get output "
                                      "metadata buffers for metadata %u",
                                      (uint32_t) i );
                        }
                        else
                        {
                            QC_INFO( "AllocateMetaDataBuffers: allocated %u output metadata "
                                     "buffers for metadata %u (tag=%s, "
                                     "bufferListId=0x%x, size=%u)",
                                     outputBufNum, (uint32_t) i, tag.c_str(), outputBufListId,
                                     outputBufSize );
                        }
                    }
                }
            }
        }
    }

    return ret;
}

QCStatus_e CameraBufferManager::PreprocessMetaDataBuffers(
        DataTree &configTree,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers, uint32_t streamNum,
        uint32_t metaDataNum, std::vector<SharedBufferPool> &bufferPools, QCNodeID_t &nodeId,
        uint32_t *pInjSensorMetaTagId )
{
    QCStatus_e ret = QC_STATUS_OK;
    std::vector<DataTree> metaDataConfigs;
    size_t bufferDescNum = 0;

    // Step 1: Obtain vendor tag ops from QCarCam.
    QCarCamRet_e qret = QCarCamMetadataGetVendorOps( &s_vendorTagOps );
    if ( QCARCAM_RET_OK != qret )
    {
        QC_ERROR( "PreprocessMetaDataBuffers: QCarCamMetadataGetVendorOps failed, status=%d",
                  (int) qret );
        ret = QC_STATUS_FAIL;
    }

    // Step 2: Register vendor tag ops with the camera_metadata library.
    if ( QC_STATUS_OK == ret )
    {
        int rc = set_camera_metadata_vendor_ops( &s_vendorTagOps );
        if ( 0 != rc )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: set_camera_metadata_vendor_ops failed, rc=%d",
                      rc );
            ret = QC_STATUS_FAIL;
        }
        else
        {
            QC_INFO( "PreprocessMetaDataBuffers: vendor tag ops registered successfully" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = configTree.Get( "metaDataConfigs", metaDataConfigs );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: failed to get metaDataConfigs" );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        bufferDescNum = buffers.size();

        // Initialize ring state vector with one entry per metadata config.
        // Entries for non-INPUT_METADATA configs will have empty bufferIds and are
        // skipped by AdvanceAndUpdateMetaDataBuffer.
        m_metaDataRingStates.clear();
        m_metaDataRingStates.resize( metaDataNum );
    }

    for ( size_t i = 0; ( QC_STATUS_OK == ret ) && ( i < metaDataNum ); i++ )
    {
        DataTree &metaCfg = metaDataConfigs[i];
        std::string tag = metaCfg.Get<std::string>( "tag", "" );
        uint32_t bufferListId = metaCfg.Get<uint32_t>( "bufferListId", 0 );
        std::vector<uint32_t> bufferIds =
                metaCfg.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        uint32_t bufferNum = (uint32_t) bufferIds.size();
        uint32_t bufferListType = QCARCAM_GET_BUFFERLIST_TYPE( bufferListId );
        size_t bufferSize = (size_t) metaCfg.Get<uint32_t>( "bufferSize", 0 );

        // Only INPUT_METADATA buffers carry camera_metadata structures.
        // For INJECTION_SENSOR_METADATA, the metadata bufferIds are raw sensor header buffers that
        // will be loaded with binary camera_metadata data directly by the injection thread at
        // runtime. They must NOT be pre-initialised with place_camera_metadata here.
        if ( QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA != bufferListType )
        {
            QC_DEBUG( "PreprocessMetaDataBuffers: skipping non-INPUT_METADATA buffer list "
                      "for metadata %u (tag=%s, bufferListId=0x%x)",
                      (uint32_t) i, tag.c_str(), bufferListId );
            continue;
        }

        // ----------------------------------------------------------------
        // Step 1: resolve QCarCam tag ID from the tag name string
        // ----------------------------------------------------------------
        auto mapIt = s_tagNameToQccIdMap.find( tag );
        if ( mapIt == s_tagNameToQccIdMap.end() )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: unknown metadata tag '%s' for metadata %u",
                      tag.c_str(), (uint32_t) i );
            ret = QC_STATUS_BAD_ARGUMENTS;
            break;
        }
        QCarCamMetadataTagId_e qccId = mapIt->second;

        // ----------------------------------------------------------------
        // Step 2: QCarCamGetMetaDataTagId – resolve camera_metadata tag ID
        // ----------------------------------------------------------------
        uint32_t tagId = 0;
        QCarCamRet_e qret = QCarCamGetMetaDataTagId( qccId, &tagId );
        if ( QCARCAM_RET_OK != qret )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: QCarCamGetMetaDataTagId failed for "
                      "tag '%s' (qccId=%u), metadata %u, status=%d",
                      tag.c_str(), (uint32_t) qccId, (uint32_t) i, (int) qret );
            ret = QC_STATUS_FAIL;
            break;
        }
        QC_INFO( "PreprocessMetaDataBuffers: resolved tag '%s': qccId=%u, tagId=%u", tag.c_str(),
                 (uint32_t) qccId, tagId );

        // Populate the ring state for this INPUT_METADATA config so that
        // AdvanceAndUpdateMetaDataBuffer can update it at runtime.
        m_metaDataRingStates[i].tagId = tagId;
        m_metaDataRingStates[i].bufferListId = bufferListId;
        m_metaDataRingStates[i].bufferIds = bufferIds;
        m_metaDataRingStates[i].bufferSize = bufferSize;
        m_metaDataRingStates[i].currentIdx = 0;

        // Determine the initial metadata value for this tag (if any)
        uint32_t initialValue = 0;
        void *pData = nullptr;
        size_t count = 0;

        if ( "TUNING_FEATURE_1_MODE" == tag )
        {
            initialValue = metaCfg.Get<uint32_t>( "Feature1Mode", 0 );
            pData = &initialValue;
            count = 1;
        }
        else if ( "TUNING_FEATURE_2_MODE" == tag )
        {
            initialValue = metaCfg.Get<uint32_t>( "Feature2Mode", 0 );
            pData = &initialValue;
            count = 1;
        }
        // For INJECTION_SENSOR_METADATA and other tags, pData remains nullptr (no initial entry
        // written)

        // ----------------------------------------------------------------
        // Steps 3-5: place_camera_metadata / add_camera_metadata_entry /
        //            get_camera_metadata_entry_count – for each buffer
        // ----------------------------------------------------------------
        for ( size_t k = 0; ( QC_STATUS_OK == ret ) && ( k < bufferNum ); k++ )
        {
            uint32_t bufferIdx = bufferIds[k];
            if ( bufferIdx >= (uint32_t) bufferDescNum )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: bufferIdx %u out of range for "
                          "metadata %u (bufferDescNum=%zu)",
                          bufferIdx, (uint32_t) i, bufferDescNum );
                ret = QC_STATUS_OUT_OF_BOUND;
                break;
            }

            QCBufferDescriptorBase_t &bufDesc = buffers[bufferIdx];
            if ( nullptr == bufDesc.pBuf )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: buffer is null for metadata %u, "
                          "bufferIdx %u",
                          (uint32_t) i, bufferIdx );
                ret = QC_STATUS_INVALID_BUF;
                break;
            }

            // Determine the effective buffer size.
            // Priority: (1) user-configured bufferSize, (2) actual allocated buffer size
            // from AllocateMetaDataBuffers, (3) auto-calculated bufferSize via
            // CalculateMetaDataBufferSize() as a final safety fallback.
            size_t metaBufSize = ( bufferSize > 0 )     ? bufferSize
                                 : ( bufDesc.size > 0 ) ? bufDesc.size
                                                        : CalculateMetaDataBufferSize( tag );

            // Step 3: place_camera_metadata – initialise camera_metadata structure
            camera_metadata_t *pMetaData = place_camera_metadata(
                    bufDesc.pBuf, metaBufSize, kMaxMetadataTagNum, kMaxMetadataTagData );
            if ( nullptr == pMetaData )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: place_camera_metadata failed for "
                          "metadata %u, bufferIdx %u, bufSize=%zu",
                          (uint32_t) i, bufferIdx, metaBufSize );
                ret = QC_STATUS_FAIL;
                break;
            }

            // Step 4: add_camera_metadata_entry – write initial tag value (if applicable)
            if ( ( nullptr != pData ) && ( count > 0 ) )
            {
                int rc = add_camera_metadata_entry( pMetaData, tagId, pData, count );
                if ( 0 != rc )
                {
                    QC_ERROR( "PreprocessMetaDataBuffers: add_camera_metadata_entry failed "
                              "for tag '%s', tagId=%u, metadata %u, bufferIdx %u, rc=%d",
                              tag.c_str(), tagId, (uint32_t) i, bufferIdx, rc );
                    ret = QC_STATUS_FAIL;
                    break;
                }

                // Step 5: get_camera_metadata_entry_count – verify and log
                size_t entryCount = get_camera_metadata_entry_count( pMetaData );
                QC_DEBUG( "PreprocessMetaDataBuffers: tag=%s, tagId=%u, "
                          "bufferListId=0x%x, bufferIdx=%u, entryCount=%zu",
                          tag.c_str(), tagId, bufferListId, bufferIdx, entryCount );
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            QC_INFO( "PreprocessMetaDataBuffers: preprocessed %u buffers for metadata %u "
                     "(tag=%s, tagId=%u, bufferListId=0x%x)",
                     bufferNum, (uint32_t) i, tag.c_str(), tagId, bufferListId );
        }
    }

    // For INJECTION_SENSOR_METADATA: also pre-initialize the header buffers as camera_metadata_t
    // structures with a placeholder INJECTION_SENSOR_METADATA entry.
    //
    // The qcx server expects inputCommonMetadata to be a valid camera_metadata_t buffer
    // containing the INJECTION_SENSOR_METADATA tag with the raw sensor metadata blob as
    // its value.  The injection thread will update this placeholder entry at runtime with
    // the actual sensor metadata read from the header file, using update_camera_metadata_entry.
    for ( size_t i = 0; ( QC_STATUS_OK == ret ) && ( i < metaDataNum ); i++ )
    {
        DataTree &metaCfg = metaDataConfigs[i];
        std::string tag = metaCfg.Get<std::string>( "tag", "" );

        if ( "INJECTION_SENSOR_METADATA" != tag )
        {
            continue;
        }

        DataTree injCfgDt;
        if ( QC_STATUS_OK != metaCfg.Get( "InjectionConfig", injCfgDt ) )
        {
            continue;
        }

        std::vector<uint32_t> headerBufIds =
                injCfgDt.Get<uint32_t>( "headerBufferIds", std::vector<uint32_t>{} );
        uint32_t headerDataSize = injCfgDt.Get<uint32_t>( "headerBufferSize", 0 );

        if ( headerBufIds.empty() )
        {
            QC_INFO( "PreprocessMetaDataBuffers: no header buffers for INJECTION_SENSOR_METADATA "
                     "metadata %u, skipping header pre-initialization",
                     (uint32_t) i );
            continue;
        }

        // Resolve tagId for INJECTION_SENSOR_METADATA
        uint32_t injTagId = 0;
        QCarCamRet_e qret = QCarCamGetMetaDataTagId( QCARCAM_METADATA_TAG_INJECTION_SENSOR_METADATA,
                                                     &injTagId );
        if ( QCARCAM_RET_OK != qret )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: QCarCamGetMetaDataTagId failed for "
                      "INJECTION_SENSOR_METADATA (metadata %u), status=%d",
                      (uint32_t) i, (int) qret );
            ret = QC_STATUS_FAIL;
            break;
        }
        QC_INFO( "PreprocessMetaDataBuffers: resolved INJECTION_SENSOR_METADATA "
                 "tagId=%u for header buffers (metadata %u)",
                 injTagId, (uint32_t) i );

        for ( uint32_t headerBufIdx : headerBufIds )
        {
            if ( headerBufIdx >= (uint32_t) bufferDescNum )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: header bufferIdx %u out of range "
                          "(bufferDescNum=%zu) for metadata %u",
                          headerBufIdx, bufferDescNum, (uint32_t) i );
                ret = QC_STATUS_OUT_OF_BOUND;
                break;
            }

            QCBufferDescriptorBase_t &headerBufDesc = buffers[headerBufIdx];
            if ( nullptr == headerBufDesc.pBuf )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: header buffer is null for "
                          "bufferIdx %u (metadata %u)",
                          headerBufIdx, (uint32_t) i );
                ret = QC_STATUS_INVALID_BUF;
                break;
            }

            // Use the actual allocated buffer size; fall back to auto-calculated size
            size_t metaBufSize =
                    ( headerBufDesc.size > 0 )
                            ? headerBufDesc.size
                            : CalculateMetaDataBufferSize( "INJECTION_SENSOR_METADATA" );

            // Initialize the camera_metadata_t structure in the header buffer
            camera_metadata_t *pMetaData = place_camera_metadata(
                    headerBufDesc.pBuf, metaBufSize, kMaxMetadataTagNum, kMaxMetadataTagData );
            if ( nullptr == pMetaData )
            {
                QC_ERROR( "PreprocessMetaDataBuffers: place_camera_metadata failed for "
                          "header buffer %u, bufSize=%zu (metadata %u)",
                          headerBufIdx, metaBufSize, (uint32_t) i );
                ret = QC_STATUS_FAIL;
                break;
            }

            // Add a placeholder INJECTION_SENSOR_METADATA entry (zero-filled).
            // The injection thread will overwrite this with the actual sensor metadata
            // blob at runtime via update_camera_metadata_entry.
            if ( headerDataSize > 0 )
            {
                std::vector<uint8_t> placeholder( headerDataSize, 0 );
                int rc = add_camera_metadata_entry( pMetaData, injTagId, placeholder.data(),
                                                    headerDataSize );
                if ( 0 != rc )
                {
                    QC_ERROR( "PreprocessMetaDataBuffers: add_camera_metadata_entry "
                              "failed for INJECTION_SENSOR_METADATA placeholder, "
                              "header buffer %u, headerDataSize=%u, rc=%d",
                              headerBufIdx, headerDataSize, rc );
                    ret = QC_STATUS_FAIL;
                    break;
                }
                QC_INFO( "PreprocessMetaDataBuffers: pre-initialized header buffer %u "
                         "with INJECTION_SENSOR_METADATA placeholder (%u bytes)",
                         headerBufIdx, headerDataSize );
            }
            else
            {
                // No data size configured: initialize empty camera_metadata_t only.
                // The injection thread must add the entry at runtime if needed.
                QC_INFO( "PreprocessMetaDataBuffers: pre-initialized header buffer %u "
                         "with empty camera_metadata_t (headerDataSize=0, "
                         "injection thread will add entry at runtime)",
                         headerBufIdx );
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            QC_INFO( "PreprocessMetaDataBuffers: pre-initialized %u header buffers for "
                     "INJECTION_SENSOR_METADATA metadata %u (injTagId=%u, headerDataSize=%u)",
                     (uint32_t) headerBufIds.size(), (uint32_t) i, injTagId, headerDataSize );
        }
    }

    // Step 4: Resolve INJECTION_SENSOR_METADATA tag ID if requested.
    // The injection thread uses this ID to update the placeholder entry in the header
    // buffer's camera_metadata_t structure at runtime via update_camera_metadata_entry.
    if ( ( QC_STATUS_OK == ret ) && ( nullptr != pInjSensorMetaTagId ) )
    {
        QCarCamRet_e injQret = QCarCamGetMetaDataTagId(
                QCARCAM_METADATA_TAG_INJECTION_SENSOR_METADATA, pInjSensorMetaTagId );
        if ( QCARCAM_RET_OK != injQret )
        {
            QC_ERROR( "PreprocessMetaDataBuffers: QCarCamGetMetaDataTagId failed for "
                      "INJECTION_SENSOR_METADATA, status=%d",
                      (int) injQret );
            ret = QC_STATUS_FAIL;
        }
        else
        {
            QC_INFO( "PreprocessMetaDataBuffers: resolved INJECTION_SENSOR_METADATA "
                     "tagId=%u",
                     *pInjSensorMetaTagId );
        }
    }

    return ret;
}

QCStatus_e CameraBufferManager::GetCurrentCommonMetaDataBuffer( uint32_t &bufferListId,
                                                                uint32_t &bufferIdx ) const
{
    QCStatus_e ret = QC_STATUS_OK;

    bufferListId = 0;
    bufferIdx = 0;

    /*
     * Iterate the metadata ring states populated by PreprocessMetaDataBuffers and return
     * the first INPUT_METADATA ring buffer slot that is currently in use.  Only entries
     * with a non-empty bufferIds vector were populated for INPUT_METADATA configs (the
     * non-INPUT_METADATA configs are skipped during preprocessing and remain empty).
     *
     * The state's currentIdx is updated by AdvanceAndUpdateMetaDataBuffer each time
     * SampleCamera advances the ring with a new TUNING_FEATURE value, so the value
     * returned here always reflects the most recent ring slot for the qcx server to
     * consume.
     */
    bool found = false;
    for ( const MetaDataRingState_t &state : m_metaDataRingStates )
    {
        if ( ( !state.bufferIds.empty() ) && ( !found ) )
        {
            bufferListId = state.bufferListId;
            bufferIdx = state.currentIdx;
            found = true;
        }
    }

    return ret;
}

QCStatus_e CameraBufferManager::AdvanceAndUpdateMetaDataBuffer(
        size_t metaDataIdx, const void *pData, size_t count,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
        uint32_t &newBufferIdx )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( metaDataIdx >= m_metaDataRingStates.size() )
    {
        QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: invalid metaDataIdx %u (ringStates=%u)",
                  (uint32_t) metaDataIdx, (uint32_t) m_metaDataRingStates.size() );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( ( QC_STATUS_OK == ret ) && ( nullptr == pData || 0 == count ) )
    {
        QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: invalid pData or count for "
                  "metaDataIdx %u",
                  (uint32_t) metaDataIdx );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( QC_STATUS_OK == ret )
    {
        MetaDataRingState_t &state = m_metaDataRingStates[metaDataIdx];
        size_t bufferNum = state.bufferIds.size();

        if ( 0 == bufferNum )
        {
            QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: no buffers for metaDataIdx %u "
                      "(non-INPUT_METADATA config or not yet initialized)",
                      (uint32_t) metaDataIdx );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        // Advance to the next ring buffer slot
        uint32_t nextIdx = 0;
        uint32_t globalBufIdx = 0;
        if ( QC_STATUS_OK == ret )
        {
            nextIdx = ( state.currentIdx + 1U ) % (uint32_t) bufferNum;
            globalBufIdx = state.bufferIds[nextIdx];

            if ( globalBufIdx >= (uint32_t) buffers.size() )
            {
                QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: globalBufIdx %u out of range "
                          "(buffers.size=%u) for metaDataIdx %u",
                          globalBufIdx, (uint32_t) buffers.size(), (uint32_t) metaDataIdx );
                ret = QC_STATUS_OUT_OF_BOUND;
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            QCBufferDescriptorBase_t &bufDesc = buffers[globalBufIdx];
            if ( nullptr == bufDesc.pBuf )
            {
                QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: null buffer for metaDataIdx %u, "
                          "ringIdx %u (globalBufIdx %u)",
                          (uint32_t) metaDataIdx, nextIdx, globalBufIdx );
                ret = QC_STATUS_INVALID_BUF;
            }

            if ( QC_STATUS_OK == ret )
            {
                // Determine buffer size: use configured bufferSize if available, else use
                // descriptor size.
                size_t metaBufSize = ( state.bufferSize > 0 ) ? state.bufferSize
                                     : ( bufDesc.size > 0 )   ? bufDesc.size
                                                              : CalculateMetaDataBufferSize( "" );

                // Reset the buffer and write the new tag value.
                camera_metadata_t *pMeta = place_camera_metadata(
                        bufDesc.pBuf, metaBufSize, kMaxMetadataTagNum, kMaxMetadataTagData );
                if ( nullptr == pMeta )
                {
                    QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: place_camera_metadata failed "
                              "for metaDataIdx %u, ringIdx %u, bufSize=%zu",
                              (uint32_t) metaDataIdx, nextIdx, metaBufSize );
                    ret = QC_STATUS_FAIL;
                }

                if ( QC_STATUS_OK == ret )
                {
                    int rc = add_camera_metadata_entry( pMeta, state.tagId, pData, count );
                    if ( 0 != rc )
                    {
                        QC_ERROR( "AdvanceAndUpdateMetaDataBuffer: add_camera_metadata_entry "
                                  "failed for metaDataIdx %u, ringIdx %u, tagId=%u, rc=%d",
                                  (uint32_t) metaDataIdx, nextIdx, state.tagId, rc );
                        ret = QC_STATUS_FAIL;
                    }
                }

                if ( QC_STATUS_OK == ret )
                {
                    // Commit the new ring buffer index.
                    state.currentIdx = nextIdx;
                    newBufferIdx = nextIdx;

                    QC_DEBUG( "AdvanceAndUpdateMetaDataBuffer: metaDataIdx=%u, "
                              "newBufferIdx=%u, tagId=%u, bufferListId=0x%x",
                              (uint32_t) metaDataIdx, nextIdx, state.tagId, state.bufferListId );
                }
            }
        }
    }

    return ret;
}

}   // namespace sample
}   // namespace QC
