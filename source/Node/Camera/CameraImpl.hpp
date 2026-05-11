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

#define MAX_QUERY_TIMES 20

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
 * 
 * @param width                   Camera Frame width
 * 
 * @param height                  Camera Frame height
 *
 * @param bufferNum               Buffer count set to camera
 *
 * @param submitRequestPattern    Buffer submit request pattern
 *
 * @param format                  Camera frame format
 *
 * @param bufferIds               The indices of buffers for each camera frame
 *
 */
typedef struct
{
    uint32_t streamId;
    uint32_t width;
    uint32_t height;
    uint32_t submitRequestPattern;
    QCImageFormat_e format;
    std::vector<uint32_t> bufferIds;
} CameraStreamConfig_t;

typedef struct
{
    uint32_t bufferListId;
    std::vector<uint32_t> bufferIds;
} CameraMetaDataConfig_t;

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
 * @brief Camera metadata buffer structure
 *
 * @param pCamMetaDataDescs         The pointer to a list of camera metadata buffer descriptors
 * @param pQcarCamMetaDataBuffers   The pointer to a list of QCarCamBuffer_t
 * @param bufferList                Buffer list of camera metadata
 *
 */
typedef struct
{
    BufferDescriptor_t *pCamMetaDataDescs = nullptr;
    QCarCamBuffer_t *pQcarCamMetaDataBuffers = nullptr;
    QCarCamBufferList_t bufferList = { 0 };
} CameraMetaDataBuffers_t;

/**
 * @brief Configuration structure for Camera Node
 * 
 * @param inputId                   Camera input id
 * 
 * @param srcId                     Input source identifier, see QCarCamInputSrc_t
 *
 * @param clientId                  Client id for multi client usecase, set to 0 by
 *                                  default for single client usecase
 *
 * @param inputMode                 The input mode id is the index into
 *                                  QCarCamInputModes_t pModex
 *
 * @param ispUseCase                ISP use case defined by qcarcam
 *
 * @param camFrameDropPattern       Frame drop patten defined by qcarcam, Set to 0
 *                                  when frame drop is not used
 *
 * @param opMode                    Operation mode defined by qcarcam
 *
 * @param bAllocator                Flag to indicate if node is buffer allocator
 *
 * @param bRequestMode              Flag to set request buffer mode
 *
 * @param bPrimary                  Flag to indicate if the session is primary or
 *                                  not, used for multi client usecase
 *
 * @param bRecovery                 Flag to enable the self-recovery for the session
 *
 * @param bEnalbleMetaData          Flag to enable metadata feature
 *
 * @param bMultiStreamFrameReady    Flag to set multiple streams frame ready event in one callback
 *
 * @param streamConfigs             Configuration array for each stream.
 *
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
     * @return QC_STATUS_OK on success, others on failure
     */
    CameraImplConfig_t &GetConifg() { return m_config; }

    /**
     * @brief Get the monitor configuration structure object
     *
     * @return QC_STATUS_OK on success, others on failure
     */
    CameraImplMonitorConfig_t &GetMonitorConifg() { return m_monitorConfig; }

    /**
     * @brief Retrieves the current state of the Camera Node.
     *
     * This function returns the current operational state of the Camera Node,
     * which may indicate whether the node is initialized, running, stopped, or in
     * an error state.
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
    QCStatus_e SubmitAllBuffers();
    QCStatus_e ImportBuffers();
    QCStatus_e UnImportBuffers();
    void ClearFrameBufferMap();
    void ClearMetaDataMap();
    void ClearFrameBuffers();
    void ClearMetaDataBuffers();

    QCStatus_e QueryInputs();
    QCStatus_e GetInputsInfo( CameraInputs_t *pCamInputs );
    QCStatus_e GetFrame( const QCarCamFrameInfo_t &camFrameInfo, uint32_t &bufferListId,
                         uint32_t &bufferIdx );
    QCStatus_e ValidateConfig( const CameraImplConfig_t *pConfig );

    void FrameCallback( CameraFrameDescriptor_t *pFrame );
    void EventCallback( const uint32_t eventId, const QCarCamEventPayload_t *pPayLoad );

    static QCarCamRet_e QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                        const QCarCamEventPayload_t *pPayload, void *pPrivateData );
    QCarCamRet_e QcarcamEventCb( const QCarCamHndl_t hndl, const uint32_t eventId,
                                 const QCarCamEventPayload_t *pPayload );

    QCarCamColorFmt_e GetQcarCamFormat( QCImageFormat_e colorFormat );

private:
    QCNodeID_t &m_nodeId;
    Logger &m_logger;
    CameraImplConfig_t m_config;
    CameraImplMonitorConfig_t m_monitorConfig;
    QCObjectState_e m_state;

    bool m_bRequestMode;
    bool m_bIsPrimary;
    bool m_enableMetaData;
    bool m_bRecovery;
    bool m_bReservedOK = false;
    bool m_bQCarCamInitialized = false;
    bool m_bRequestPatternMode = false;

    size_t m_streamNum;
    size_t m_metaDataNum;
    size_t m_maxBufCnt;
    uint32_t m_inputId;
    std::atomic<uint32_t> m_requestId;
    uint32_t m_clientId;
    uint32_t m_refStreamId = QCNODE_CAMERA_MAX_STREAM_NUM;
    uint32_t m_submitRequestPattern[QCNODE_CAMERA_MAX_STREAM_NUM];
    uint64_t m_frameId[QCNODE_CAMERA_MAX_STREAM_NUM] = { 0 };

    std::mutex m_mutex;
    std::queue<uint32_t> m_freeBufIdxQueue[QCNODE_CAMERA_MAX_STREAM_NUM];
    CameraStreamConfig_t m_streamConfigs[QCNODE_CAMERA_MAX_STREAM_NUM];

    std::vector<CameraFrameBuffers_t> m_frameBuffers;
    std::vector<CameraMetaDataBuffers_t> m_metaDataBuffers;

    std::unordered_map<uint64_t, CameraFrameBuffers_t> m_frameBufferMap;
    std::unordered_map<uint64_t, CameraMetaDataBuffers_t> m_metaDataBufferMap;

    QCarCamHndl_t m_QcarCamHndl;
    QCNodeEventCallBack_t m_callback = nullptr;

    QC_DECLARE_NODETRACE();
};

}   // namespace Node
}   // namespace QC
#endif   // QC_NODE_CAMERA_IMPL_HPP
