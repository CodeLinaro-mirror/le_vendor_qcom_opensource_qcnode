// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_CAMERA_BUFFER_DESCRIPTOR_HPP
#define QC_CAMERA_BUFFER_DESCRIPTOR_HPP

#include "QC/Infras/Memory/ImageDescriptor.hpp"

namespace QC
{
namespace Memory
{

/** @brief The maximum input/output stream number of QCNode Camera */
#define QCNODE_CAMERA_MAX_INPUT_STREAM_NUM 4U
#define QCNODE_CAMERA_MAX_STREAM_NUM 32U

/** @brief The maximum streamId of QCNode Camera */
#define QCNODE_CAMERA_MAX_STREAM_ID 255U

/** @brief The maximum buffer number for each stream of QCNode Camera */
#define QCNODE_CAMERA_MAX_BUFFER_NUM 20U

/**
 * @brief Camera buffer identification for each request
 * @param bufferListId      The index of buffer group for each request
 * @param bufferIdx         The index of buffer within the buffer group
 * @param metaBufferListId  The index of per-stream metadata buffer group (for stream requests).
 *                          Must be in QCARCAM_BUFFERLIST_ID_INPUT_METADATA range.
 *                          Set to 0 when per-stream metadata is not used.
 * @param metaBufferId      The index of per-stream metadata buffer within the metadata group.
 */
typedef struct
{
    uint32_t bufferListId = 0;
    uint32_t bufferIdx = 0;
    uint32_t metaBufferListId = 0;   ///< Per-stream metadata buffer list ID (0 = not used)
    uint32_t metaBufferId = 0;       ///< Per-stream metadata buffer index
} CameraBufferRequest_t;

/**
 * @brief Metadata type enumeration for QCNode Camera metadata descriptors.
 *
 * Identifies the specific ISP/camera metadata feature that a
 * CameraMetaDataDescriptor_t represents.  The type drives how the
 * implementation initialises and updates the underlying camera_metadata
 * buffer before each QCarCamSubmitRequest call.
 *
 * CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE:
 *   Maps to QCARCAM_METADATA_TAG_TUNING_FEATURE_1_MODE or
 *   QCARCAM_METADATA_TAG_TUNING_FEATURE_2_MODE.
 *   A single uint32_t value (feature1Mode or feature2Mode) is written as a
 *   sticky entry into the INPUT_METADATA buffer via inputCommonMetadata on
 *   every request.  The specific tag is determined by the "TUNING_FEATURE_1_MODE"
 *   or "TUNING_FEATURE_2_MODE" tag name string in the metadata config.
 *
 * CAMERA_METADATA_TYPE_ISP_INJECTION:
 *   Maps to QCARCAM_METADATA_TAG_INJECTION_SENSOR_METADATA.
 *   The INPUT buffer carries raw sensor header data loaded from a file.
 *   The QCARCAM_REQUEST_FLAG_INJECTION flag must be set in the request.
 *
 * CAMERA_METADATA_TYPE_GENERIC:
 *   Any other QCarCam vendor tag.  The tag ID and data pointer are resolved
 *   at initialisation time from the tag name string.
 */
typedef enum CameraMetaDataType
{
    CAMERA_METADATA_TYPE_UNKNOWN = 0,           ///< Uninitialized / invalid type
    CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE,   ///< TUNING_FEATURE_1_MODE or TUNING_FEATURE_2_MODE
                                                ///< sticky metadata
    CAMERA_METADATA_TYPE_ISP_INJECTION,         ///< ISP standalone injection metadata
    CAMERA_METADATA_TYPE_GENERIC,               ///< Generic QCarCam vendor tag metadata
    CAMERA_METADATA_TYPE_MAX
} CameraMetaDataType_e;

/**
 * @brief Descriptor for QCNode Shared Camera Frame Descriptor.
 * This structure represents the camera frame descriptor for QCNode. It
 * extends the ImageDescriptor and includes additional members specific
 * to camera frames.
 *
 * Inherited Members from ImageDescriptor:
 * @param name The name of the buffer.
 * @param pBuf The virtual address of the dma buffer.
 * @param size The dma size of the buffer.
 * @param type The type of the buffer.
 * @param alignment The alignment of the buffer.
 * @param cache The cache type of the buffer.
 * @param allocatorType The allocaor type used for allocation the buffer.
 * @param dmaHandle The dmaHandle of the buffer.
 * @param pid The process ID of the buffer.
 * @param validSize The size of valid data currently stored in the buffer.
 * @param offset The offset of the valid buffer within the shared buffer.
 * @param id A identifier assigned by the user application to distinguish the buffer.
 * @param format The image format.
 * @param batchSize The image batch size.
 * @param width The image width in pixels.
 * @param height The image height in pixels.
 * @param stride The image stride along the width in bytes for each plane.
 * @param actualHeight The actual height of the image in scanlines for each plane.
 * @param planeBufSize The actual buffer size of the image for each plane, calculated as (stride *
 * actualHeight + padding size).
 * @param numPlanes The number of image planes.
 *
 * New Members:
 * @param timestamp The hardware timestamp of the frame in nanoseconds.
 * @param timestampQGPTP The Generic Precision Time Protocol (GPTP) timestamp in nanoseconds.
 * @param frameIdx The index of the camera frame.
 * @param flags Indicating the error state of the buffer.
 * @param streamId The identifier for the Qcarcam buffer list.
 */
typedef struct CameraFrameDescriptor : public ImageDescriptor
{
public:
    CameraFrameDescriptor() : ImageDescriptor() {}

    /**
     * @brief Sets up the camera frame descriptor from another buffer descriptor base object.
     * @param[in] other The camera frame descriptor object from which buffer members are copied.
     * @return The updated camera frame descriptor object.
     */
    CameraFrameDescriptor &operator=( const QCBufferDescriptorBase &other );

    uint64_t timestamp;
    uint64_t timestampQGPTP;
    uint32_t frameIdx;
    uint32_t flags;
    uint32_t streamId;
} CameraFrameDescriptor_t;

/**
 * @brief Descriptor for QCNode Shared Camera MetaData Descriptor.
 * This structure represents the camera metadata descriptor for QCNode. It
 * extends the ImageDescriptor and includes additional members specific
 * to camera frames.
 *
 * Inherited Members from ImageDescriptor:
 * @param name The name of the buffer.
 * @param pBuf The virtual address of the dma buffer.
 * @param size The dma size of the buffer.
 * @param type The type of the buffer.
 * @param alignment The alignment of the buffer.
 * @param cache The cache type of the buffer.
 * @param allocatorType The allocaor type used for allocation the buffer.
 * @param dmaHandle The dmaHandle of the buffer.
 * @param pid The process ID of the buffer.
 * @param validSize The size of valid data currently stored in the buffer.
 * @param offset The offset of the valid buffer within the shared buffer.
 * @param id A identifier assigned by the user application to distinguish the buffer.
 * @param format The image format.
 * @param batchSize The image batch size.
 * @param width The image width in pixels.
 * @param height The image height in pixels.
 * @param stride The image stride along the width in bytes for each plane.
 * @param actualHeight The actual height of the image in scanlines for each plane.
 * @param planeBufSize The actual buffer size of the image for each plane, calculated as (stride *
 * actualHeight + padding size).
 * @param numPlanes The number of image planes.
 *
 * New Members:
 * @param requestId The unique id of request in QCarCamera.
 * @param streamRequestNum The number of request for each stream.
 * @param syncId Used for request synchronization across multiple cameras that are frame synced.
 * @param flags Indicating the error state of the buffer.
 * @param inputBuffer Input buffer for injection usecase.
 * @param inputCommonMetadata Common input metadata to be applied to all inputs.
 * @param inputMetadata Input metadata for each of the individual inputs.
 * @param outputMetadata Output metadata for each of the individual inputs.
 * @param streamRequests Output buffer indices for each stream.
 */
typedef struct CameraMetaDataDescriptor : public ImageDescriptor
{
public:
    CameraMetaDataDescriptor()
        : ImageDescriptor(),
          requestId( 0 ),
          streamRequestNum( 0 ),
          syncId( 0 ),
          flags( 0 )
    {}

    uint32_t requestId;
    uint32_t streamRequestNum;
    uint32_t syncId;
    uint32_t flags;

    CameraBufferRequest_t inputBuffer;
    CameraBufferRequest_t inputCommonMetadata;
    CameraBufferRequest_t inputMetadata[QCNODE_CAMERA_MAX_INPUT_STREAM_NUM];
    CameraBufferRequest_t outputMetadata[QCNODE_CAMERA_MAX_INPUT_STREAM_NUM];
    CameraBufferRequest_t streamRequests[QCNODE_CAMERA_MAX_STREAM_NUM];
} CameraMetaDataDescriptor_t;

}   // namespace Memory
}   // namespace QC

#endif   // QC_CAMERA_BUFFER_DESCRIPTOR_HPP
