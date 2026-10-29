// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_SAMPLE_DATA_ONLINE_HPP
#define QC_SAMPLE_DATA_ONLINE_HPP

#include "QC/sample/SampleIF.hpp"
#include <deque>
#include <mutex>

namespace QC
{
namespace sample
{

/// @brief qcnode::sample::SampleDataOnline
///
/// SampleDataOnline that to demonstate how to use the QC component Remap
class SampleDataOnline : public SampleIF
{
public:
    SampleDataOnline();
    ~SampleDataOnline();

    /// @brief Initialize the remap
    /// @param name the sample unique instance name
    /// @param config the sample config key value map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the remap
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the remap
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief deinitialize the remap
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

private:
    enum class MetaCommand : uint32_t
    {
        META_COMMAND_DATA,
        META_COMMAND_MODEL_INFO,
    };

    struct Meta
    { /* total 128 bytes */
        uint64_t payloadSize;
        uint64_t Id;
        uint64_t timestamp;
        uint32_t numOfDatas;
        MetaCommand command;
        uint8_t reserved[96];
    };

    struct MetaModelInfo
    { /* total 128 bytes */
        uint64_t payloadSize;
        uint64_t Id;
        uint64_t timestamp;
        uint32_t numOfDatas;
        MetaCommand command;
        uint32_t numInputs;
        uint32_t numOuputs;
        uint8_t reserved[88];
    };

    struct DataMeta
    {
        QCBufferType_e dataType;
        uint32_t size;
        uint8_t reserved[120];
    };

    struct DataImageProps
    {
        QCImageFormat_e format;
        uint32_t batchSize;
        uint32_t width;
        uint32_t height;
        uint32_t stride[QC_NUM_IMAGE_PLANES];
        uint32_t actualHeight[QC_NUM_IMAGE_PLANES];
        uint32_t planeBufSize[QC_NUM_IMAGE_PLANES];
        uint32_t numPlanes;

        DataImageProps &operator=( const ImageProps_t &other )
        {
            this->format = other.format;
            this->batchSize = other.batchSize;
            this->width = other.width;
            this->height = other.height;
            uint32_t numPlanes = std::min( other.numPlanes, (uint32_t) QC_NUM_IMAGE_PLANES );
            std::copy( other.stride, other.stride + numPlanes, this->stride );
            std::copy( other.actualHeight, other.actualHeight + numPlanes, this->actualHeight );
            std::copy( other.planeBufSize, other.planeBufSize + numPlanes, this->planeBufSize );
            this->numPlanes = numPlanes;
            return *this;
        }

        void To( ImageProps_t &other )
        {
            other.format = this->format;
            other.batchSize = this->batchSize;
            other.width = this->width;
            other.height = this->height;
            uint32_t numPlanes = std::min( this->numPlanes, (uint32_t) QC_NUM_IMAGE_PLANES );
            std::copy( this->stride, this->stride + numPlanes, other.stride );
            std::copy( this->actualHeight, this->actualHeight + numPlanes, other.actualHeight );
            std::copy( this->planeBufSize, this->planeBufSize + numPlanes, other.planeBufSize );
            other.numPlanes = numPlanes;
        }
    };

    struct DataImageMeta
    {
        QCBufferType_e dataType;
        uint32_t size;
        DataImageProps imageProps;
        uint8_t reserved[120 - sizeof( DataImageProps )];
    };

    struct DataTensorProps
    {
        QCTensorType_e tensorType;
        uint32_t dims[QC_NUM_TENSOR_DIMS];
        uint32_t numDims;

        DataTensorProps &operator=( const TensorProps_t &other )
        {
            this->tensorType = other.tensorType;
            uint32_t numDims = std::min( other.numDims, (uint32_t) QC_NUM_TENSOR_DIMS );
            std::copy( other.dims, other.dims + numDims, this->dims );
            this->numDims = numDims;
            return *this;
        }

        void To( TensorProps_t &other )
        {
            other.tensorType = this->tensorType;
            uint32_t numDims = std::min( this->numDims, (uint32_t) QC_NUM_TENSOR_DIMS );
            std::copy( this->dims, this->dims + numDims, other.dims );
            other.numDims = numDims;
        }
    };

    struct DataTensorMeta
    {
        QCBufferType_e dataType;
        uint32_t size;
        DataTensorProps tensorProps;
        float quantScale;
        int32_t quantOffset;
        char name[64];
        uint8_t reserved[48 - sizeof( DataTensorProps )];
    };

private:
    QCStatus_e ParseConfig( SampleConfig_t &config );
    QCStatus_e ReceiveData( Meta &meta );
    QCStatus_e ReplyModelInfo();
    QCStatus_e ReceiveMain();
    void ThreadSubMain();
    void ThreadPubMain();

private:
    uint32_t m_poolSize = 4;

    int m_port = 6666;

    int m_server = -1;
    int m_client = -1;
    bool m_bOnline = false;

    ModelInOutInfo_t m_modelInOutInfo;
    bool m_bHasInfInfo = false;

    std::string m_inputTopicName;
    std::string m_modelInOutInfoTopicName;
    std::string m_outputTopicName;

    std::thread m_subThread;
    std::thread m_pubThread;
    std::deque<SharedBufferPool> m_bufferPools;
    bool m_stop;

    uint32_t m_timeout = 2;

    DataSubscriber<DataFrames_t> m_sub;
    DataPublisher<DataFrames_t> m_pub;

    DataSubscriber<ModelInOutInfo_t> m_modelInOutInfoSub;

    QCAllocationCache_e m_bufferCache = QC_CACHEABLE;

    std::vector<uint8_t> m_payload;
};   // class SampleDataOnline

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_DATA_ONLINE_HPP
