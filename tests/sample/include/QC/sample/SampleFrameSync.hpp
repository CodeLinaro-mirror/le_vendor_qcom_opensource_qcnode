// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_FRAME_SYNC_HPP
#define QC_SAMPLE_FRAME_SYNC_HPP
#include "QC/sample/SampleIF.hpp"

namespace QC
{
namespace sample
{

/// @brief qcnode::sample::SampleFrameSync
///
/// SampleFrameSync that to synchronize multiple inputs
class SampleFrameSync : public SampleIF
{
public:
    SampleFrameSync();
    ~SampleFrameSync();

    /// @brief Initialize the FrameSync
    /// @param name the sample unique instance name
    /// @param config the sample config key value map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the FrameSync
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the FrameSync
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief deinitialize the FrameSync
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

#ifdef QC_ENABLE_HS
    /// @brief Get the runnable callback for HeteroScheduler
    /// @return The runnable callback function
    std::function<void( const std::uint32_t *, std::size_t )> GetRunnableCallback() override;
#endif

    /// @brief Wait until all input subscribers have received at least one frame.
    ///
    /// Called after Start() and before starting the HeteroScheduler.  Blocks until
    /// every camera input topic has data available, indicating the upstream camera
    /// pipeline is live and the HS scheduling cycle can safely begin.
    ///
    /// @return QC_STATUS_OK when all inputs are ready; QC_STATUS_FAIL if stopped early.
    QCStatus_e WaitReady() override;

private:
    QCStatus_e ParseConfig( SampleConfig_t &config );
    void threadWindowMain();
    void threadBufferTimestampMain();
    void Execute();
    void ExecuteWindowSync();
    void ExecuteBufferTimestamp();
    void PublishFrames( std::vector<DataFrames_t> &selectedFrames, uint64_t frameId );

#ifdef QC_ENABLE_HS
    void RunnableCallback( const std::uint32_t *rids, std::size_t count );
#endif

private:
    typedef enum
    {
        FRAME_SYNC_MODE_WINDOW,             ///< time-window based collection (existing)
        FRAME_SYNC_MODE_BUFFER_TIMESTAMP,   ///< peek-based best-match timestamp selection
    } FrameSyncMode_e;

private:
    std::thread m_thread;
    bool m_stop;

    FrameSyncMode_e m_syncMode;

    uint32_t m_number;
    uint32_t m_windowMs;
    uint32_t m_timestampThresholdMs;
    uint32_t m_queueDepth;

    std::vector<std::string> m_inputTopicNames;
    std::string m_outputTopicName;

    std::vector<uint32_t> m_perms; /* the permutation */

    /* HS mode only: subset of input-topic indices that WaitReady() must block on
     * before the HS cycle starts. Empty (not configured) => wait on all inputs. */
    std::vector<uint32_t> m_waitReadyIndices;

    std::vector<DataSubscriber<DataFrames_t>> m_subs;
    DataPublisher<DataFrames_t> m_pub;
};   // class SampleFrameSync

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_FRAME_SYNC_HPP
