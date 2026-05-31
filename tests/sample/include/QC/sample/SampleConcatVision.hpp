// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_NODE_CONCAT_VISION_HPP
#define QC_SAMPLE_NODE_CONCAT_VISION_HPP

#include "QC/sample/SampleIF.hpp"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace QC
{
namespace sample
{

/// @brief qcnode::sample::SampleConcatVision
///
/// Fuses VIT embeddings into the LLM input embedding sequence.
/// Receives float32 VIT output directly and replaces image-token positions
/// in the pre-loaded input_embeds with the corresponding VIT embedding vectors.
class SampleConcatVision : public SampleIF
{
public:
    SampleConcatVision();
    ~SampleConcatVision();

    /// @brief Initialize the node
    /// @param name unique instance name
    /// @param config key-value configuration map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the processing thread
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the processing thread
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief Deinitialize the node and release resources
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

private:
    void       ThreadMain();
    QCStatus_e ParseConfig( SampleConfig_t &config );

    /// @brief Load int64 token IDs from a binary file
    QCStatus_e LoadInputIds( const std::string &filepath );

    /// @brief Load float32 input embeddings from a binary file
    /// @note Must be called after LoadInputIds
    QCStatus_e LoadInputEmbeds( const std::string &filepath );

    /// @brief Replace image-token embeddings with VIT output embeddings
    /// @param input  float32 VIT embeddings [vit_seq_len, hidden_dim]
    /// @param output float32 fused embeddings [batch, seq_len, hidden_dim]
    QCStatus_e Execute( float *input, float *output );

private:
    std::string m_inputTopicName;
    std::string m_outputTopicName;

    std::thread m_thread;
    bool        m_stop = false;

    uint32_t         m_poolSize = 4;
    SharedBufferPool m_embdesPool;
    TensorProps_t    m_embdesTsProps;   // derived from loaded file dimensions in ParseConfig

    DataSubscriber<DataFrames_t> m_sub;
    DataPublisher<DataFrames_t>  m_pub;

    BufferManager *m_pBufMgr = nullptr;

    // Image token replacement configuration
    int m_imageTokenIndex = 151655;

    // VIT output shape (configurable)
    int m_vitSeqLen    = 529;
    int m_vitHiddenDim = 2048;

    // Pre-loaded tensors
    std::vector<int64_t> m_inputIds;
    std::vector<float>   m_inputEmbeds;

    // Tensor dimensions derived from loaded files
    int m_inputIdsBatchSize = 0;
    int m_inputIdsSeqLen    = 0;
    int m_inputEmbedsDim    = 0;

};   // class SampleConcatVision

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_NODE_CONCAT_VISION_HPP