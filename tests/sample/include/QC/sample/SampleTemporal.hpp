// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_NODE_TEMPORAL_HPP
#define QC_SAMPLE_NODE_TEMPORAL_HPP

#include "QC/sample/SampleIF.hpp"

namespace QC
{
namespace sample
{

/// @brief qcnode::sample::SampleTemporal
///
/// SampleTemporal that to demonstate how to handle temporal kind of AI model
class SampleTemporal : public SampleIF
{
public:
    SampleTemporal();
    ~SampleTemporal();

    /// @brief Initialize the Temporal
    /// @param name the sample unique instance name
    /// @param config the sample config key value map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the Temporal
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the Temporal
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief deinitialize the Temporal
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

#ifdef QC_ENABLE_HS
    /// @brief Get the runnable callback for HeteroScheduler
    /// @return The runnable callback function
    std::function<void( const std::uint32_t *, std::size_t )> GetRunnableCallback() override;
#endif

private:
    void ThreadMain();
    void Execute();
    QCStatus_e ParseConfig( SampleConfig_t &config );

    QCStatus_e FillTensor( TensorDescriptor_t &tensorDesc, float scale, int32_t offset,
                           float value );

#ifdef QC_ENABLE_HS
    void RunnableCallback( const std::uint32_t *rids, std::size_t count );
#endif

private:
    struct TemporalContext
    {
        std::shared_ptr<SharedBuffer_t> temporal = nullptr;
        TensorDescriptor_t initTempTs;
        TensorProps_t temporalTsProps;
        float temporalQuantScale;
        int32_t temporalQuantOffset;
        uint32_t temporalIndex = 0;
    };

private:
    std::string m_inputTopicName;
    std::string m_outputTopicName;

    std::thread m_thread;

    uint32_t m_windowMs;

    uint32_t m_number;
    std::vector<TemporalContext> m_temporal;

    bool m_bHasUseFlag = false; /* use temporal flag can be optional */
    std::shared_ptr<SharedBuffer_t> m_useFlag = nullptr;
    TensorProps_t m_useFlagTsProps;
    float m_useFlagQuantScale;
    int32_t m_useFlagQuantOffset;
    TensorDescriptor_t m_useFlagTs;

    bool m_stop;
    uint64_t m_frameId = 0;

    DataSubscriber<DataFrames_t> m_sub;
    DataPublisher<DataFrames_t> m_pub;

    BufferManager *m_pBufMgr = nullptr;
};   // class SampleTemporal

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_NODE_TEMPORAL_HPP
