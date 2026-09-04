// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_CAMERA_IMPL_HPP
#define QC_NODE_CAMERA_IMPL_HPP

#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "QC/Infras/NodeTrace/NodeTrace.hpp"
#include "QC/Node/Camera.hpp"

#if defined( __QNXNTO__ )
#include "QC/Infras/Memory/PMEMUtils.hpp"
#else
#include "QC/Infras/Memory/DMABUFFUtils.hpp"
#endif

namespace QC
{
namespace Node
{

using namespace QC::Memory;
using namespace QC::Node;

#define MAX_QUERY_TIMES 20

/** @brief Maximum number of metadata tags per metadata buffer */
const uint32_t MAX_METADATA_TAG_NUM = 50;

/** @brief Maximum data size per metadata tag in bytes */
const uint32_t MAX_METADATA_TAG_DATA = 65536;

/**
 * @brief Configuration structure for ISP injection usecase.
 *
 * This structure holds all parameters required to configure the ISP injection.
 * It is used when the metadata tag is "INJECTION_SENSOR_METADATA".
 *
 * The injection use case supports three types of input data files:
 *
 * 1. Raw frame data (inputBufferListId / inputBufferIds):
 *    The actual image frame data to be injected into the ISP pipeline.
 *    Loaded into QCARCAM_BUFFERLIST_TYPE_INPUT buffers.
 *    Mapped to QCarCamRequest_t::inputBuffer on each request.
 *
 * 2. Sensor header / metadata (headerBufferListId / headerBufferIds):
 *    Per-frame sensor metadata (exposure, gain, etc.) in camera_metadata format.
 *    Loaded into QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA buffers.
 *    Mapped to QCarCamRequest_t::inputCommonMetadata on each request.
 *
 * 3. EEPROM calibration data (eepromBufferListId / eepromBufferIds):
 *    Static EEPROM / lens-calibration data.  Typically loaded once and reused
 *    for every request.
 *    Loaded into a separate QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA buffer list.
 *    Mapped to QCarCamRequest_t::inputMetadata[0] on each request.
 *
 * @param inputId                       Camera input ID used to capture the original image.
 * @param inputMode                     Input mode index in QCarCamInputModes_t.
 * @param inputTuningParamFeature1Mode  Tuning parameter feature 1 mode for ISP.
 * @param inputTuningParamFeature2Mode  Tuning parameter feature 2 mode for ISP.
 * @param inputSceneMode                Scene mode for ISP tuning.
 *
 * @param inputBufferListId             Buffer list ID for raw frame input buffers
 *                                      (QCARCAM_BUFFERLIST_TYPE_INPUT range).
 * @param inputBufferIds                Indices of raw frame buffers in QCNodeInit::buffers.
 * @param format                        Raw data format of the raw frame input buffer
 *                                      (QCarCamColorFmt_e). Resolved by CameraConfig from a
 *                                      "mipiraw_*" / "plain16_*" string at parse time.
 *
 * @param headerBufferListId            Buffer list ID for sensor header metadata buffers
 *                                      (QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA range).
 * @param headerBufferIds               Indices of header metadata buffers in QCNodeInit::buffers.
 *
 * @param eepromBufferListId            Buffer list ID for EEPROM calibration buffers
 *                                      (QCARCAM_BUFFERLIST_TYPE_INPUT_METADATA range).
 * @param eepromBufferIds               Indices of EEPROM buffers in QCNodeInit::buffers.
 */
typedef struct
{
    uint32_t inputId;
    uint32_t inputMode;
    uint32_t inputTuningParamFeature1Mode;
    uint32_t inputTuningParamFeature2Mode;
    uint32_t inputSceneMode;

    /* Raw frame data (INPUT type) */
    uint32_t inputBufferListId;
    std::vector<uint32_t> inputBufferIds;
    QCarCamColorFmt_e
            format;   ///< Raw data format of the raw frame input buffer; resolved by
                      ///< CameraConfig from a "mipiraw_*" / "plain16_*" tag at parse time.

    /* Raw frame input buffer plane dimensions */
    uint32_t width;    ///< Width of raw frame input buffer in pixels
    uint32_t height;   ///< Height of raw frame input buffer in lines
    uint32_t stride;   ///< Stride of raw frame input buffer in bytes

    /* Sensor header / per-frame metadata (INPUT_METADATA type) */
    uint32_t headerBufferListId;
    std::vector<uint32_t> headerBufferIds;

    /* EEPROM calibration data (INPUT_METADATA type, separate buffer list) */
    uint32_t eepromBufferListId;
    std::vector<uint32_t> eepromBufferIds;
} CameraInjectConfig_t;

/**
 * @brief Camera metadata buffer structure used for injection buffer lists.
 *
 * Holds the allocated buffer descriptors and QCarCam buffer objects for a
 * single injection buffer list (raw frame input, sensor header, or EEPROM).
 *
 * @param pCamMetaDataDescs         Pointer to an array of camera metadata buffer descriptors.
 * @param pQcarCamMetaDataBuffers   Pointer to an array of QCarCamBuffer_t.
 * @param bufferList                QCarCam buffer list descriptor.
 */
typedef struct
{
    BufferDescriptor_t *pCamMetaDataDescs = nullptr;
    QCarCamBuffer_t *pQcarCamMetaDataBuffers = nullptr;
    QCarCamBufferList_t bufferList = { 0 };
} CameraMetaDataBuffers_t;

/**
 * @brief Camera MetaData config
 *
 * @param bufferListId            The index of metadata buffer group.
 *                                For INJECTION_SENSOR_METADATA: INPUT_METADATA buffer list (sensor
 * metadata). For TUNING_FEATURE_1_MODE / TUNING_FEATURE_2_MODE: INPUT_METADATA.
 *
 * @param metaDataType            The resolved metadata type enum (filled during init from the
 *                                JSON "tag" string by CameraConfig::GetMetaDataType). Drives all
 *                                downstream behaviour; the original tag string is not retained.
 * @param injectionConfig         ISP injection parameters (only used when metaDataType is
 *                                CAMERA_METADATA_TYPE_ISP_INJECTION)
 * @param bufferIds               The indices of buffers for each camera metadata
 */
typedef struct
{
    uint32_t bufferListId;
    CameraMetaDataType_e metaDataType;
    CameraInjectConfig_t injectionConfig;
    std::vector<uint32_t> bufferIds;
    uint32_t outputBufferListId;             ///< Buffer list ID for output metadata (0 = not used)
    std::vector<uint32_t> outputBufferIds;   ///< Buffer indices for output metadata
} CameraMetaDataConfig_t;

/**
 * @brief Camera Inputs structure
 *
 * @param pCameraInputs     Pointer to the list of qcarcam inputs info
 * @param pCamInputModes    Pointer to the list of qcarcam input modes for each input
 * @param numInputs         Number of qcarcam inputs
 *
 */
typedef struct
{
    QCarCamInput_t *pCameraInputs;
    QCarCamInputModes_t *pCamInputModes;
    uint32_t numInputs;
} CameraInputs_t;

/**
 * @brief Camera Stream config
 *
 * @param streamId                Camera stream id
 * @param contextId               Context id this stream belongs to. Streams that belong to the
 *                                same qcx context (as defined in the qcarcam usecase XML) MUST
 *                                share a contextId, and qcx-distinct contexts MUST use distinct
 *                                contextIds. CameraImpl emits one QCarCamRequest_t per
 *                                contextId per submission cycle so streams from different
 *                                contexts never share streamRequests[] (qcx server rejects
 *                                cross-context batching with "All the streams in the request
 *                                are not part of same context").
 * @param width                   Camera Frame width
 * @param height                  Camera Frame height
 * @param submitRequestPattern    Buffer submit request pattern. The pattern's reference stream
 *                                (pattern == 0) is enforced per contextId.
 * @param format                  Camera frame format
 * @param bufferIds               The indices of buffers for each camera frame
 *
 */
typedef struct
{
    uint32_t streamId;
    uint32_t contextId;
    uint32_t width;
    uint32_t height;
    uint32_t submitRequestPattern;
    QCImageFormat_e format;
    std::vector<uint32_t> bufferIds;
} CameraStreamConfig_t;

/**
 * @brief Camera frame buffer structure
 *
 * @param pCamFrameDescs         The pointer to a list of camera frame buffer descriptors
 * @param pQcarCamFrameBuffers   The pointer to a list of QCarCamBuffer_t
 * @param bufferList             Buffer list of camera frame
 *
 */
typedef struct
{
    CameraFrameDescriptor_t *pCamFrameDescs = nullptr;
    QCarCamBuffer_t *pQcarCamFrameBuffers = nullptr;
    QCarCamBufferList_t bufferList = { 0 };
} CameraFrameBuffers_t;

/**
 * @brief Configuration structure for Camera Node
 *
 * @param inputId                   Camera input id
 * @param srcId                     Input source identifier, see QCarCamInputSrc_t
 * @param clientId                  Client id for multi client usecase, set to 0 by default
 * @param inputMode                 The input mode id is the index into QCarCamInputModes_t pModex
 * @param ispUseCase                ISP use case defined by qcarcam
 * @param camFrameDropPattern       Frame drop pattern defined by qcarcam, Set to 0 when not used
 * @param camFrameDropPeriod        Frame drop period defined by qcarcam
 * @param opMode                    Operation mode defined by qcarcam
 * @param bRequestMode              Flag to set request buffer mode
 * @param bPrimary                  Flag to indicate if the session is primary or not
 * @param bRecovery                 Flag to enable the self-recovery for the session
 * @param bEnalbleMetaData          Flag to enable metadata feature
 * @param bMultiStreamFrameReady    Flag to set multiple streams frame ready event in one callback
 * @param streamConfigs             Configuration array for each stream.
 * @param metaDataConfigs           Configuration array for each metadata.
 *
 */
typedef struct Camera_Config : public QCNodeConfigBase_t
{
    uint32_t inputId;
    uint32_t srcId;
    uint32_t clientId;
    uint32_t inputMode;
    uint32_t ispUseCase;
    uint32_t camFrameDropPattern;
    uint8_t camFrameDropPeriod;
    uint32_t opMode;
    bool bRequestMode;
    bool bPrimary;
    bool bRecovery;
    bool bEnalbleMetaData;
    bool bMultiStreamFrameReady;
    std::vector<CameraStreamConfig_t> streamConfigs;
    std::vector<CameraMetaDataConfig_t> metaDataConfigs;
} CameraImplConfig_t;

// TODO
typedef struct CameraImplMonitorConfig : public QCNodeMonitoringBase_t
{
    bool bEnablePerf;
} CameraImplMonitorConfig_t;

/**
 * @brief Camera Node Event Data Structure
 * @param eventId The QCarcamera callback event type.
 * @param pPayload The pointer of QCarcamera event payload.
 */
typedef struct
{
    uint32_t eventId;
    const void *pPayload;
} CameraImpEvent_t;


class CameraImpl
{

public:
    /**
     * @brief Construct a new CameraImpl object
     */
    CameraImpl( QCNodeID_t &nodeId, Logger &logger );

    /**
     * @brief Destroy the CameraImpl object
     */
    ~CameraImpl();

    /**
     * @brief Initialize CameraImpl object
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Initialize( QCNodeEventCallBack_t callback,
                           std::vector<std::reference_wrapper<QCBufferDescriptorBase>> &buffers );

    /**
     * @brief Start the CameraImpl object
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Start();

    /**
     * @brief Process a camera frame
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc );

    /**
     * @brief Stop the CameraImpl object
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Stop();

    /**
     * @brief Deinit the CameraImpl object
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e DeInitialize();

    /**
     * @brief Get the CameraImpl configuration structure object
     *
     * @return Reference to the CameraImplConfig_t
     */
    CameraImplConfig_t &GetConifg() { return m_config; }

    /**
     * @brief Get the monitor configuration structure object
     *
     * @return Reference to the CameraImplMonitorConfig_t
     */
    CameraImplMonitorConfig_t &GetMonitorConifg() { return m_monitorConfig; }

    /**
     * @brief Retrieves the current state of the Camera Node.
     *
     * @return The current state of the Camera Node.
     */
    QCObjectState_e GetState();

private:
    QCStatus_e ReleaseFrame( const CameraFrameDescriptor_t *pFrame );
    QCStatus_e SubmitRequest( const CameraFrameDescriptor_t *pFrame );
    QCStatus_e SubmitRequest( const CameraMetaDataDescriptor_t *pMetaData );

    QCStatus_e
    SetFrameBuffers( std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers );
    QCStatus_e
    SetMetaDataBuffers( std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers );

    QCStatus_e RegisterInjectionBufferList(
            const std::vector<uint32_t> &bufIds, uint32_t listId, const char *label,
            std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
            std::vector<CameraMetaDataBuffers_t> &injectionBuffers,
            QCarCamColorFmt_e colorFmt = QCARCAM_FMT_MAX, uint32_t planeWidth = 0,
            uint32_t planeHeight = 0, uint32_t planeStride = 0 );

    QCStatus_e SubmitAllBuffers();
    QCStatus_e ImportBuffers();
    QCStatus_e UnImportBuffers();
    void ClearFrameBuffers();
    void ClearMetaDataBuffers();

    QCStatus_e QueryInputs();
    QCStatus_e GetInputsInfo( CameraInputs_t *pCamInputs );
    QCStatus_e GetFrame( const QCarCamFrameInfo_t &camFrameInfo, uint32_t &bufferListId,
                         uint32_t &bufferIdx );
    QCStatus_e ValidateConfig( const CameraImplConfig_t &config );

    void FrameCallback( CameraFrameDescriptor_t *pFrame );
    void ReturnInvalidFrame( CameraFrameDescriptor_t &frame );
    void EventCallback( const uint32_t eventId, const QCarCamEventPayload_t *pPayLoad );

    static QCarCamRet_e QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                        const QCarCamEventPayload_t *pPayload,
                                        void *pPrivateData ) noexcept;
    QCarCamRet_e QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                 const QCarCamEventPayload_t *pPayload );

    QCarCamColorFmt_e GetQcarCamFormat( QCImageFormat_e colorFormat );

private:
    QCNodeID_t &m_nodeId;
    Logger &m_logger;
    CameraImplConfig_t m_config;
    CameraImplMonitorConfig_t m_monitorConfig;
    std::atomic<QCObjectState_e> m_state;

    bool m_bRequestMode;
    bool m_bIsPrimary;
    bool m_enableMetaData;
    bool m_enableInjection;
    bool m_bRecovery;
    bool m_bRequestPatternMode = false;

    size_t m_streamNum;
    size_t m_metaDataNum;
    size_t m_maxBufCnt;
    uint32_t m_inputId;
    uint32_t m_clientId;
    std::atomic<uint32_t> m_requestId;
    CameraBufferRequest_t m_currentInputCommonMetadata;

    std::mutex m_mutex;

    uint32_t m_submitRequestPattern[QCNODE_CAMERA_MAX_STREAM_NUM];
    uint64_t m_frameId[QCNODE_CAMERA_MAX_STREAM_NUM] = { 0 };
    CameraStreamConfig_t m_streamConfigs[QCNODE_CAMERA_MAX_STREAM_NUM];
    std::queue<uint32_t> m_freeBufIdxQueue[QCNODE_CAMERA_MAX_STREAM_NUM];

    std::vector<CameraFrameBuffers_t> m_frameBuffers;
    std::vector<CameraMetaDataBuffers_t> m_metaDataBuffers;

    std::unordered_map<uint32_t, uint32_t> m_refStreamIdByContext;
    std::unordered_map<uint32_t, uint32_t> m_streamIdToIndexMap;
    std::unordered_map<uint64_t, CameraFrameBuffers_t> m_frameBufferMap;
    std::unordered_map<uint64_t, CameraMetaDataBuffers_t> m_metaDataBufferMap;

    QCarCamHndl_t m_QcarCamHndl;
    QCNodeEventCallBack_t m_callback = nullptr;

    QC_DECLARE_NODETRACE();
};

}   // namespace Node
}   // namespace QC
#endif   // QC_NODE_CAMERA_IMPL_HPP
