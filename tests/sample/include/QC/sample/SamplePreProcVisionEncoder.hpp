// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_PRE_PROC_VISION_ENCODER_HPP
#define QC_SAMPLE_PRE_PROC_VISION_ENCODER_HPP

#include "QC/sample/SampleIF.hpp"
#include "OpenclIface.hpp"

#include <CL/cl.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace QC
{
namespace sample
{

using namespace QC::Node;
using namespace QC::libs::OpenclIface;

/// @brief qcnode::sample::SamplePreProcVisionEncoder
///
/// Vision encoder pre-processor for Qwen2.5-VL pipeline.
/// Accepts RGB888 input (from an upstream CL2DFlex node) and produces
/// ufixed_point16 pixel_values tensor.
/// Supports both CPU and GPU (OpenCL) execution, selected via the
/// "processor" configuration key (cpu / gpu).
class SamplePreProcVisionEncoder : public SampleIF
{
public:
    SamplePreProcVisionEncoder();
    ~SamplePreProcVisionEncoder();

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
    QCStatus_e ParseConfig( SampleConfig_t &config );
    void       ThreadMain();

    /// @brief Compute smart-resize output dimensions (multiples of patch_size * merge_size)
    void SmartResize( uint32_t height, uint32_t width, uint32_t &out_height,
                      uint32_t &out_width );

    // CPU path
    /// @brief CPU path: normalize RGB to ufixed_point16 in a single pass
    QCStatus_e PreprocessCPU( DataFrames_t &frames );

    /// @brief Normalize RGB888 image and quantize directly to ufixed_point16 in one pass.
    /// @note Patch extraction order: gh -> gw -> c -> t -> ph -> pw
    void PreprocessRGBToUint16( const uint8_t *rgb_data, uint32_t height, uint32_t width,
                                uint16_t *output );

    // GPU path
    /// @brief GPU path: normalize and quantize via OpenCL kernel
    QCStatus_e PreprocessGPU( DataFrames_t &frames );

private:
    // Processor selection (cpu or gpu)
    QCProcessorType_e m_processor = QC_PROCESSOR_CPU;

    // Patch extraction parameters (configurable)
    uint32_t m_patchSize         = 14;
    uint32_t m_temporalPatchSize = 2;
    uint32_t m_mergeSize         = 2;
    // IMAGE_FACTOR = m_patchSize * m_mergeSize (derived, not a separate config key)

    // Normalization parameters (OpenAI CLIP defaults)
    float m_imageMean[3] = { 0.48145466f, 0.4578275f, 0.40821073f };
    float m_imageStd[3]  = { 0.26862954f, 0.26130258f, 0.27577711f };

    // Quantization parameters for ufixed_point16 output
    float   m_pixelValuesScale  = 0.00006009246135363355f;
    int32_t m_pixelValuesOffset = -29825;

    // Smart-resize pixel bounds
    uint32_t m_minPixels = 4u * 28u * 28u;
    uint32_t m_maxPixels = 16384u * 28u * 28u;

    // Resize target and computed dimensions
    uint32_t m_targetHeight  = 0;
    uint32_t m_targetWidth   = 0;
    uint32_t m_resizedHeight = 0;
    uint32_t m_resizedWidth  = 0;

    // Patch grid dimensions (derived from resize result and patch parameters)
    uint32_t m_gridT      = 0;
    uint32_t m_gridH      = 0;
    uint32_t m_gridW      = 0;
    uint32_t m_numPatches = 0;
    uint32_t m_patchDim   = 0;

    // Configuration
    uint32_t            m_poolSize    = 4;
    QCAllocationCache_e m_bufferCache = QC_CACHEABLE;

    std::string m_inputTopicName;
    std::string m_outputTopicName;

    // Thread and output buffer pool
    std::thread      m_thread;
    SharedBufferPool m_tensorPool;
    bool             m_stop = false;

    DataSubscriber<DataFrames_t> m_sub;
    DataPublisher<DataFrames_t>  m_pub;

    BufferManager *m_pBufMgr = nullptr;

    // OpenCL resources (GPU path only)
    OpenclSrv m_openclSrv;
    cl_kernel m_kernel = nullptr;

};   // class SamplePreProcVisionEncoder

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_PRE_PROC_VISION_ENCODER_HPP