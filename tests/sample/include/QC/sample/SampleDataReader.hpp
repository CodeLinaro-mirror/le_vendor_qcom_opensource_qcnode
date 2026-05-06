// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_SAMPLE_DATAREADER_HPP
#define QC_SAMPLE_DATAREADER_HPP

#include "QC/sample/SampleIF.hpp"
#include <mutex>

namespace QC
{
namespace sample
{

/// @brief qcnode::sample::SampleDataReader
///
/// SampleDataReader that simulate sensor frames
class SampleDataReader : public SampleIF
{
public:
    SampleDataReader();
    ~SampleDataReader();

    /// @brief Initialize the Data Reader
    /// @param name the sample unique instance name
    /// @param config the sample config key value map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the Data Reader
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the Data Reader
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief deinitialize the Data Reader
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

#ifdef QC_ENABLE_HS
    /// @brief Get the runnable callback for HeteroScheduler
    /// @return The runnable callback function
    std::function<void( const std::uint32_t *, std::size_t )> GetRunnableCallback();
#endif

private:
    QCStatus_e ParseConfig( SampleConfig_t &config );
    void ThreadMain();
    void Execute();
    QCStatus_e LoadImage( std::shared_ptr<SharedBuffer_t> image, std::string path );
    QCStatus_e LoadTensor( std::shared_ptr<SharedBuffer_t> tensor, std::string path );
#ifdef QC_ENABLE_HS
    void RunnableCallback( const std::uint32_t *rids, std::size_t count );
    void RunnableCallbackWithSleep( const std::uint32_t *rids, std::size_t count );
#endif

private:
    typedef enum
    {
        DATA_READER_TYPE_IMAGE,
        DATA_READER_TYPE_TENSOR,
    } DataReaderType_e;

    typedef struct
    {
        DataReaderType_e type;
        QCImageFormat_e format;
        uint32_t width;
        uint32_t height;
        TensorProps_t tensorProps;
        std::string dataPath;
    } DataReaderConfig_t;

    float m_fps;
    std::vector<DataReaderConfig_t> m_configs;
    uint32_t m_numOfDataReaders;
    uint32_t m_offset;

    uint32_t m_poolSize = 4;
    std::string m_topicName;

    uint32_t m_index;
    uint64_t m_frameId;

    std::thread m_thread;
    std::vector<SharedBufferPool> m_bufferPools;
    bool m_stop;

    DataPublisher<DataFrames_t> m_pub;
    QCAllocationCache_e m_bufferCache = QC_CACHEABLE;

#ifdef QC_ENABLE_HS
    bool m_bEnableHsSleep = false;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFrameTime;
    uint64_t m_frameIntervalNs = 0;
#endif

};   // class SampleDataReader

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_DATAREADER_HPP
