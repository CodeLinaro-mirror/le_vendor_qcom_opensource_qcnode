// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_CAMERA_BUFFER_MANAGER_HPP
#define QC_SAMPLE_CAMERA_BUFFER_MANAGER_HPP

#include "QC/Common/DataTree.hpp"
#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Node/Camera.hpp"
#include "QC/sample/SharedBufferPool.hpp"

namespace QC
{
namespace sample
{

using namespace QC::Memory;

/**
 * @brief CameraBufferManager manages DMA buffer allocation for QCNode Camera.
 *
 * Allocates frame buffers for each camera output stream and metadata buffers
 * for each metadata config (INJECTION_SENSOR_METADATA input buffers, TUNING_FEATURE metadata
 * buffers, etc.), then registers them into QCNodeInit_t::buffers so that
 * CameraImpl can set them to QCarCam.
 */
class CameraBufferManager
{
public:
    CameraBufferManager( Logger &logger );
    ~CameraBufferManager();

    /**
     * @brief Allocate DMA frame buffers for all camera output streams.
     *
     * Reads stream configs from configTree (key "streamConfigs"), allocates one
     * SharedBufferPool per stream, and appends the resulting buffer descriptors
     * into buffer vector.
     *
     * @param[in]     configTree   DataTree containing "streamConfigs" array.
     * @param[in,out] buffers      Buffer vector to place allocated buffers.
     * @param[in]     streamNum    Number of streams to allocate.
     * @param[in,out] bufferPools  Vector of SharedBufferPool; pools [0, streamNum) are filled.
     * @param[in]     nodeId       Node ID used for buffer pool naming.
     * @return QC_STATUS_OK on success, error code on failure.
     */
    QCStatus_e
    AllocateFrameBuffers( DataTree &configTree,
                          std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
                          uint32_t streamNum, std::vector<SharedBufferPool> &bufferPools,
                          QCNodeID_t &nodeId );

    /**
     * @brief Allocate DMA metadata buffers for all camera metadata configs.
     *
     * Reads metadata configs from configTree (key "metaDataConfigs"), allocates one
     * SharedBufferPool per metadata config entry, and appends the resulting buffer
     * descriptors into buffer vector.
     *
     * For INJECTION_SENSOR_METADATA metadata configs, this function also allocates the injection
     * input buffers (raw frame data) specified in InjectionConfig.inputBufferIds.
     *
     * @param[in]     configTree   DataTree containing "metaDataConfigs" array.
     * @param[in,out] buffers      Buffer vector to place allocated buffers.
     * @param[in]     streamNum    Number of streams (offset into bufferPools).
     * @param[in]     metaDataNum  Number of metadata configs to allocate.
     * @param[in,out] bufferPools  Vector of SharedBufferPool; pools [streamNum,
     * streamNum+metaDataNum) are filled for metadata buffers. Additional pools may be appended for
     * INJECTION_SENSOR_METADATA input buffers.
     * @param[in]     nodeId       Node ID used for buffer pool naming.
     * @return QC_STATUS_OK on success, error code on failure.
     */
    QCStatus_e
    AllocateMetaDataBuffers( DataTree &configTree,
                             std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
                             uint32_t streamNum, uint32_t metaDataNum,
                             std::vector<SharedBufferPool> &bufferPools, QCNodeID_t &nodeId );

    /**
     * @brief Register vendor tag ops, preprocess allocated DMA metadata buffers, and
     *        optionally resolve the INJECTION_SENSOR_METADATA tag ID.
     *
     * This function must be called after AllocateMetaDataBuffers() and before
     * Camera::Initialize(). It performs the following steps in order:
     *
     *   1. Calls QCarCamMetadataGetVendorOps() to obtain the vendor_tag_ops_t.
     *   2. Calls set_camera_metadata_vendor_ops() to register vendor tags with the
     *      camera_metadata library.  This is required so that add_camera_metadata_entry
     *      can recognize vendor-defined tags such as INJECTION_SENSOR_METADATA
     *      (tag 0x806c0000).  Matches qcarcam_test behavior exactly.
     *   3. For each metadata config entry whose bufferListId is of type
     *      QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA, initializes every allocated buffer:
     *        a. Resolves the QCarCam metadata tag ID from the tag name string via
     *           QCarCamGetMetaDataTagId to obtain the camera_metadata tag ID.
     *        b. Calls place_camera_metadata to initialize the camera_metadata structure.
     *        c. If the tag carries an initial value (e.g. Feature1Mode for
     *           TUNING_FEATURE_1_MODE, Feature2Mode for TUNING_FEATURE_2_MODE),
     *           calls add_camera_metadata_entry to write that value.
     *        d. Calls get_camera_metadata_entry_count to verify and log the result.
     *      Non-INPUT_METADATA buffers (e.g. raw injection frame data) are skipped.
     *   4. If pInjSensorMetaTagId is non-null, resolves the camera_metadata tag ID for
     *      QCARCAM_METADATA_TAG_INJECTION_SENSOR_METADATA via QCarCamGetMetaDataTagId().
     *      The resolved ID is used by the injection thread to update the
     *      INJECTION_SENSOR_METADATA entry at runtime via update_camera_metadata_entry.
     *
     * After this call, the internal metadata ring state (m_metaDataRingStates) is
     * populated for each INPUT_METADATA config, enabling subsequent calls to
     * AdvanceAndUpdateMetaDataBuffer().
     *
     * @param[in]     configTree          DataTree containing "metaDataConfigs" array.
     * @param[in,out] buffers             Buffer vector to place allocated buffers.
     * @param[in]     streamNum           Number of streams (kept for API consistency).
     * @param[in]     metaDataNum         Number of metadata configs to preprocess.
     * @param[in,out] bufferPools         Vector of SharedBufferPool (not modified).
     * @param[in]     nodeId              Node ID used for logging.
     * @param[out]    pInjSensorMetaTagId Optional output for the resolved
     *                                    INJECTION_SENSOR_METADATA camera_metadata tag ID.
     *                                    Pass nullptr if INJECTION_SENSOR_METADATA is not enabled.
     * @return QC_STATUS_OK on success, error code on failure.
     */
    QCStatus_e PreprocessMetaDataBuffers(
            DataTree &configTree,
            std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
            uint32_t streamNum, uint32_t metaDataNum, std::vector<SharedBufferPool> &bufferPools,
            QCNodeID_t &nodeId, uint32_t *pInjSensorMetaTagId = nullptr );

    /**
     * @brief Retrieve the current input common metadata ring buffer slot for TUNING_FEATURE
     *        sticky modes.
     *
     * Returns the {bufferListId, bufferIdx} of the first INPUT_METADATA ring buffer slot
     * that is currently in use for a TUNING_FEATURE_1_MODE / TUNING_FEATURE_2_MODE
     * configuration.  This is the value that should be attached as
     * QCarCamRequest_t::inputCommonMetadata when submitting frame requests, ensuring the
     * qcx server applies the latest tuning value to every frame.
     *
     * SampleCamera calls this method before each Camera::ProcessFrameDescriptor call and
     * conveys the result to the Camera node by attaching a CameraMetaDataDescriptor (with
     * inputCommonMetadata populated) at index 1 of the NodeFrameDescriptor.  The Camera
     * node stores and replays this value on every QCarCamSubmitRequest call.
     *
     * If no TUNING_FEATURE INPUT_METADATA config has been registered, both outputs are
     * set to 0 and QC_STATUS_OK is still returned (no metadata to attach).
     *
     * The state queried here is populated by PreprocessMetaDataBuffers and updated in
     * lock-step with AdvanceAndUpdateMetaDataBuffer.
     *
     * @param[out] bufferListId  The QCarCam INPUT_METADATA buffer list ID.
     * @param[out] bufferIdx     The current ring buffer slot index within that list.
     * @return QC_STATUS_OK on success.
     */
    QCStatus_e GetCurrentCommonMetaDataBuffer( uint32_t &bufferListId, uint32_t &bufferIdx ) const;

    /**
     * @brief Advance the ring buffer index and update a camera_metadata buffer with a new value.
     *
     * Advances the ring buffer slot for the given metadata config index, resets the
     * camera_metadata structure in the new slot via place_camera_metadata, and writes
     * the provided tag data via add_camera_metadata_entry.
     *
     * The metadata ring state (tagId, bufferIds, bufferSize, currentIdx) must have been
     * populated by a prior call to PreprocessMetaDataBuffers().
     *
     * On success, the internal ring buffer index for the given metadata config is updated
     * to the new slot, and newBufferIdx is set to that slot index.
     *
     * @param[in]     metaDataIdx   Index into the metadata ring state (matches the index
     *                              used in metaDataConfigs during PreprocessMetaDataBuffers).
     * @param[in]     pData         Pointer to the new tag data to write.
     * @param[in]     count         Number of data elements pointed to by pData.
     * @param[in,out] buffers       All allocated buffer descriptors (QCNodeInit_t::buffers).
     * @param[out]    newBufferIdx  The new ring buffer slot index after advancing.
     * @return QC_STATUS_OK on success, error code on failure.
     */
    QCStatus_e AdvanceAndUpdateMetaDataBuffer(
            size_t metaDataIdx, const void *pData, size_t count,
            std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
            uint32_t &newBufferIdx );

private:
    /**
     * @brief Per-metadata-config ring buffer state used by AdvanceAndUpdateMetaDataBuffer.
     *
     * Populated during PreprocessMetaDataBuffers for each INPUT_METADATA config.
     * Non-INPUT_METADATA configs (e.g. raw injection frame data) have an empty bufferIds
     * vector and are skipped by AdvanceAndUpdateMetaDataBuffer.
     */
    struct MetaDataRingState_t
    {
        uint32_t tagId = 0;                ///< Resolved camera_metadata tag ID for this config.
        uint32_t bufferListId = 0;         ///< QCarCam buffer list ID for this metadata config.
        std::vector<uint32_t> bufferIds;   ///< Global buffer indices into QCNodeInit_t::buffers.
        size_t bufferSize = 0;             ///< Per-buffer size in bytes (0 = use descriptor size).
        uint32_t currentIdx = 0;           ///< Current ring buffer slot index.
    };

    /// @brief Ring state vector, one entry per metadata config (indexed by metaDataIdx).
    /// Populated by PreprocessMetaDataBuffers; used by AdvanceAndUpdateMetaDataBuffer.
    std::vector<MetaDataRingState_t> m_metaDataRingStates;

    Logger &m_logger;
};

}   // namespace sample
}   // namespace QC

#endif /* QC_SAMPLE_CAMERA_BUFFER_MANAGER_HPP */
