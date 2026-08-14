// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_VIDEO_CODEC_NODE_BASE_HPP
#define QC_VIDEO_CODEC_NODE_BASE_HPP

#include "QC/Node/Ifs/QCNodeIfs.hpp"
#include "QC/Node/NodeBase.hpp"
#include "VidcDrvClient.hpp"
#include <mutex>
#include <queue>
#include <sys/uio.h>
#include <unordered_map>

namespace QC::Node
{

/**
 * @brief Video Encoder Node Configuration Data Structure
 * @param params The QC component Video Encoder configuration data structure.
 */
typedef struct VidcNodeBase_Config : public QCNodeConfigBase_t
{
    ~VidcNodeBase_Config() override = default;
    uint32_t width{ 0U };     /**< in pixels */
    uint32_t height{ 0U };    /**< in pixels */
    uint32_t frameRate{ 0U }; /**< fps */
    uint32_t numInputBufferReq{ 0U };
    uint32_t numOutputBufferReq{ 0U };
    bool bInputDynamicMode{ false };
    bool bOutputDynamicMode{ false };
    QCImageFormat_e inFormat{};  /**< uncompressed type */
    QCImageFormat_e outFormat{}; /**< compressed type */
    Logger_Level_e logLevel{ LOGGER_LEVEL_ERROR };
} VidcNodeBase_Config_t;

class VidcNodeBaseConfigIfs : public NodeConfigBase
{
public:
    /**
     * @brief VidcNodeBaseConfigIfs Constructor
     * @param[in] config A reference to the configuration structure that VerifyAndSet() will
     *                   populate and that Get() returns. Owned by the derived class; the
     *                   reference must outlive this object.
     * @param[in] logger A reference to the logger to be shared and used by VidcNodeBaseConfigIfs.
     * @return None
     */
    VidcNodeBaseConfigIfs( VidcNodeBase_Config_t &config, Logger &logger )
        : NodeConfigBase( logger ),
          m_config( config )
    {}

    /**
     * @brief VidcNodeBaseConfigIfs Destructor
     * @return None
     */
    virtual ~VidcNodeBaseConfigIfs() = default;

    /**
     * @brief Verify and Load the json string, populating the bound configuration structure.
     *
     * @param[in] cfg the json configuration string
     * @param[out] errors the error string to be used to return readable error information
     *
     * @return QC_STATUS_OK on success, others on failure
     *
     * @note Overrides NodeConfigBase::VerifyAndSet. The parsed common video-codec fields are
     * written into the configuration structure bound at construction (see the ctor @p config).
     * @note This API will also initialize the logger only once.
     * And this API can be called multiple times to apply dynamic parameter settings during runtime
     * after initialization.
     */
    QCStatus_e VerifyAndSet( const std::string cfg, std::string &errors ) override;

    /**
     * @brief Get Configuration Options
     * @param[out] options The JSON configuration options string. Unused; left unchanged.
     * @return QC_STATUS_UNSUPPORTED — the base video-codec config exposes no options.
     */
    QCStatus_e GetOptions( std::string &options ) override { return QC_STATUS_UNSUPPORTED; }

    /**
     * @brief Get the Configuration Structure.
     * @return A reference to the configuration structure bound at construction.
     */
    const QCNodeConfigBase_t &Get() override { return m_config; };

protected:
    QCStatus_e ParseStaticConfig( DataTree &dt, std::string &errors,
                                  VidcNodeBase_Config_t &config );

private:
    VidcNodeBase_Config_t &m_config;
};

/** @brief base class for video codec component */
class VidcNodeBase : public NodeBase
{
public:
    /** @brief Default constructor */
    VidcNodeBase() : m_state( QC_OBJECT_STATE_INITIAL ) {}

    /** @brief Default destructor */
    virtual ~VidcNodeBase() = default;

    /**
     * @brief Init the video codec node
     * @param pName the video codec unique instance name
     * @param level the log level used , default is error level
     * @return QC_STATUS_OK on success, others on failure
     */
    virtual QCStatus_e Init( const VidcNodeBase_Config_t &config );

    /**
     * @brief deinitialize the video codec
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e DeInitialize() override;

    QCStatus_e Start() override;
    QCStatus_e Stop() override;

    QCStatus_e PostInit( void );

    QCStatus_e
    AllocateBuffer( const std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
                    uint32_t bufferIdx, VideoCodec_BufType_e bufferType );

    /**
     * @brief Inform the video codec node of an externally allocated buffer address.
     *        i.e register a buffer address with video codec node.
     *        to be used only if an external allocator allocated this buffer.
     * @note NegotiateBufferReq() should be called before to get the buffer requirements
     *       from the driver.
     * @note API type: Synchronous
     */
    QCStatus_e SetBuffer( VideoCodec_BufType_e bufferType );

    QCStatus_e FreeOutputBuffers();
    QCStatus_e FreeInputBuffers();

    QCStatus_e ValidateBuffer( const VideoFrameDescriptor &vidFrmDesc,
                               VideoCodec_BufType_e bufferType );
    QCStatus_e ValidateBuffers();
    QCStatus_e NegotiateBufferReq( VideoCodec_BufType_e bufType );

    QCStatus_e WaitForState( QCObjectState_e expectedState );

    vidc_color_format_type GetVidcFormat( QCImageFormat_e format );

protected:
    std::string m_name;
    QCObjectState_e m_state = QC_OBJECT_STATE_INITIAL;

    void EventCallback( const VideoCodec_EventType_e eventId, void *pPayload );

    VidcDrvClient m_drvClient;

    uint32_t m_bufSize[VIDEO_CODEC_BUF_TYPE_NUM];

    const VidcNodeBase_Config_t *m_pConfig = nullptr;

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>>
            m_inputBufferList; /**< set input descriptors in non-dynamic mode */
    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>>
            m_outputBufferList; /**< set output descriptors in non-dynamic mode */

    QCStatus_e ValidateFrameSubmission( const VideoFrameDescriptor_t &frameDesc,
                                        VideoCodec_BufType_e bufferType,
                                        bool requireNonZeroSize = true );

private:
    typedef enum
    {
        COMPRESSED_DATA_MODE = 0,
        YUV_OR_RGB_MODE
    } VideoCodec_BufAllocMode_e;

    QCStatus_e InitBufferForNonDynamicMode(
            const std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &buffers,
            uint32_t bufferIdx, VideoCodec_BufType_e bufferType );

    void PrintConfig();
};

}   // namespace QC::Node

#endif   // QC_VIDEO_CODEC_NODE_BASE_HPP
