// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/sample/SamplePreProcVisionEncoder.hpp"

#include <algorithm>
#include <cstring>

namespace QC
{
namespace sample
{

// OpenCL kernel: RGB888 -> normalized float -> ufixed_point16
// Patch extraction order: gh -> gw -> c -> t -> ph -> pw (matches CPU version)
// Normalization: pixel = rgb/255; normalized = (pixel - mean[c]) / std[c]
// Quantization:  out = clamp(round(normalized / scale - offset), 0, 65535)
static const char *s_pSourcePreProcVisionEncoder = R"(
__kernel void rgb_to_pixel_values(
    __global const uchar *rgb_input,
    __global ushort      *output,
    uint  width,
    uint  grid_h,
    uint  grid_w,
    uint  patch_size,
    uint  temporal_patch_size,
    float mean_r, float mean_g, float mean_b,
    float std_r,  float std_g,  float std_b,
    float scale,
    int   offset_val
)
{
    uint out_idx   = get_global_id(0);
    uint channel   = 3;
    uint patch_dim = channel * temporal_patch_size * patch_size * patch_size;
    uint total     = grid_h * grid_w * patch_dim;

    if (out_idx >= total) return;

    // Decompose flat index into (gh, gw, c, t, ph, pw)
    uint tmp = out_idx;
    uint pw  = tmp % patch_size;           tmp /= patch_size;
    uint ph  = tmp % patch_size;           tmp /= patch_size;
    uint t   = tmp % temporal_patch_size;  tmp /= temporal_patch_size;
    uint c   = tmp % channel;              tmp /= channel;
    uint gw  = tmp % grid_w;              tmp /= grid_w;
    uint gh  = tmp;

    // Pixel position in the source image (t does not shift position for single-frame input)
    uint h = gh * patch_size + ph;
    uint w = gw * patch_size + pw;

    // RGB888 layout: [H, W, C], stride = width * 3 (no padding)
    uint rgb_idx = (h * width + w) * 3 + c;

    float mean_val = (c == 0) ? mean_r : (c == 1) ? mean_g : mean_b;
    float std_val  = (c == 0) ? std_r  : (c == 1) ? std_g  : std_b;

    float pixel      = (float)rgb_input[rgb_idx] / 255.0f;
    float normalized = (pixel - mean_val) / std_val;

    float quantized = round(normalized / scale - (float)offset_val);
    quantized = max(0.0f, min(65535.0f, quantized));

    output[out_idx] = (ushort)quantized;
}
)";

SamplePreProcVisionEncoder::SamplePreProcVisionEncoder() {}
SamplePreProcVisionEncoder::~SamplePreProcVisionEncoder() {}

void SamplePreProcVisionEncoder::SmartResize( uint32_t height, uint32_t width,
                                              uint32_t &out_height, uint32_t &out_width )
{
    const uint32_t imageFactor = m_patchSize * m_mergeSize;

    uint32_t h_bar = std::max( imageFactor, ( height / imageFactor ) * imageFactor );
    uint32_t w_bar = std::max( imageFactor, ( width / imageFactor ) * imageFactor );

    if ( h_bar * w_bar > m_maxPixels )
    {
        float beta = std::sqrt( static_cast<float>( height * width ) / m_maxPixels );
        h_bar = static_cast<uint32_t>( std::floor( height / beta / imageFactor ) ) * imageFactor;
        w_bar = static_cast<uint32_t>( std::floor( width / beta / imageFactor ) ) * imageFactor;
    }
    else if ( h_bar * w_bar < m_minPixels )
    {
        float beta = std::sqrt( static_cast<float>( m_minPixels ) / ( height * width ) );
        h_bar = static_cast<uint32_t>( std::ceil( height * beta / imageFactor ) ) * imageFactor;
        w_bar = static_cast<uint32_t>( std::ceil( width * beta / imageFactor ) ) * imageFactor;
    }

    out_height = h_bar;
    out_width  = w_bar;
}

QCStatus_e SamplePreProcVisionEncoder::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    m_processor = Get( config, "processor", QC_PROCESSOR_CPU );
    if ( ( QC_PROCESSOR_CPU != m_processor ) && ( QC_PROCESSOR_GPU != m_processor ) )
    {
        QC_ERROR( "invalid processor type, only cpu and gpu are supported\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    // Patch extraction parameters
    m_patchSize         = Get( config, "patch_size", 14u );
    m_temporalPatchSize = Get( config, "temporal_patch_size", 2u );
    m_mergeSize         = Get( config, "merge_size", 2u );

    // Normalization parameters
    m_imageMean[0] = Get( config, "image_mean_r", 0.48145466f );
    m_imageMean[1] = Get( config, "image_mean_g", 0.4578275f );
    m_imageMean[2] = Get( config, "image_mean_b", 0.40821073f );
    m_imageStd[0]  = Get( config, "image_std_r", 0.26862954f );
    m_imageStd[1]  = Get( config, "image_std_g", 0.26130258f );
    m_imageStd[2]  = Get( config, "image_std_b", 0.27577711f );

    // Quantization parameters
    m_pixelValuesScale  = Get( config, "pixel_values_scale", 0.00006009246135363355f );
    m_pixelValuesOffset = Get( config, "pixel_values_offset", -29825 );

    // Smart-resize pixel bounds
    m_minPixels = Get( config, "min_pixels", 4u * 28u * 28u );
    m_maxPixels = Get( config, "max_pixels", 16384u * 28u * 28u );

    // Resize target
    m_targetHeight = Get( config, "target_height", 644u );
    m_targetWidth  = Get( config, "target_width", 644u );

    SmartResize( m_targetHeight, m_targetWidth, m_resizedHeight, m_resizedWidth );
    QC_INFO( "Smart resize: %ux%u -> %ux%u\n", m_targetWidth, m_targetHeight, m_resizedWidth,
             m_resizedHeight );

    // Derived patch grid dimensions
    m_gridT      = 1;
    m_gridH      = m_resizedHeight / m_patchSize;
    m_gridW      = m_resizedWidth / m_patchSize;
    m_numPatches = m_gridT * m_gridH * m_gridW;
    m_patchDim   = 3 * m_temporalPatchSize * m_patchSize * m_patchSize;

    QC_INFO( "Patches: %u (grid_t=%u, grid_h=%u, grid_w=%u), patch_dim=%u\n", m_numPatches,
             m_gridT, m_gridH, m_gridW, m_patchDim );

    m_poolSize = Get( config, "pool_size", 4u );
    bool bCache = Get( config, "cache", true );
    m_bufferCache = bCache ? QC_CACHEABLE : QC_CACHEABLE_NON;

    m_inputTopicName = Get( config, "input_topic", "" );
    if ( m_inputTopicName.empty() )
    {
        QC_ERROR( "no input topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_outputTopicName = Get( config, "output_topic", "" );
    if ( m_outputTopicName.empty() )
    {
        QC_ERROR( "no output topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    return ret;
}

QCStatus_e SamplePreProcVisionEncoder::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = SampleIF::Init( name );

    if ( QC_STATUS_OK == ret )
    {
        ret = ParseConfig( config );
    }

    if ( QC_STATUS_OK == ret )
    {
        QC_TRACE_INIT( [&]() {
            DataTree dt;
            dt.Set<std::string>( "name", name );
            dt.Set<std::string>( "processor",
                                 ( QC_PROCESSOR_GPU == m_processor ) ? "opencl" : "cpu" );
            return dt.Dump();
        }() );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_pBufMgr = BufferManager::Get( m_nodeId, m_logger.GetLevel() );
        if ( nullptr == m_pBufMgr )
        {
            QC_ERROR( "Failed to get buffer manager for %s %d: %s!", m_nodeId.name.c_str(),
                      m_nodeId.id, name.c_str() );
            ret = QC_STATUS_FAIL;
        }
    }

    // Output tensor pool: ufixed_point16, shape [1, grid_h, grid_w, patch_dim]
    if ( QC_STATUS_OK == ret )
    {
        TensorProps_t tensorProp;
        tensorProp.tensorType    = QC_TENSOR_TYPE_UFIXED_POINT_16;
        tensorProp.numDims       = 4;
        tensorProp.dims[0]       = 1;
        tensorProp.dims[1]       = m_gridH;
        tensorProp.dims[2]       = m_gridW;
        tensorProp.dims[3]       = m_patchDim;
        tensorProp.allocatorType = QC_MEMORY_ALLOCATOR_DMA_GPU;
        tensorProp.cache         = m_bufferCache;

        ret = m_tensorPool.Init( name + "_tensor", m_nodeId, LOGGER_LEVEL_INFO, m_poolSize,
                                 tensorProp );
    }

    // GPU path: initialize OpenCL
    if ( QC_STATUS_OK == ret && QC_PROCESSOR_GPU == m_processor )
    {
        QC_TRACE_BEGIN( "Init", {} );
        ret = m_openclSrv.Init( name.c_str(), LOGGER_LEVEL_ERROR );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Failed to init OpenCL\n" );
        }

        if ( QC_STATUS_OK == ret )
        {
            ret = m_openclSrv.LoadFromSource( s_pSourcePreProcVisionEncoder );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Failed to load OpenCL kernel source\n" );
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            ret = m_openclSrv.CreateKernel( &m_kernel, "rgb_to_pixel_values" );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Failed to create OpenCL kernel rgb_to_pixel_values\n" );
            }
        }
        QC_TRACE_END( "Init", {} );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_sub.Init( name, m_inputTopicName );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_pub.Init( name, m_outputTopicName );
    }

    return ret;
}

QCStatus_e SamplePreProcVisionEncoder::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    QC_TRACE_BEGIN( "Start", {} );
    m_stop   = false;
    m_thread = std::thread( &SamplePreProcVisionEncoder::ThreadMain, this );
    QC_TRACE_END( "Start", {} );

    return ret;
}

// =============================================================================
// CPU path implementation
// =============================================================================

void SamplePreProcVisionEncoder::PreprocessRGBToUint16( const uint8_t *rgb_data,
                                                         uint32_t height, uint32_t width,
                                                         uint16_t *output )
{
    const uint32_t channel = 3;
    const uint32_t grid_h  = height / m_patchSize;
    const uint32_t grid_w  = width / m_patchSize;
    uint32_t       out_idx = 0;

    // Normalize and quantize in a single pass: gh -> gw -> c -> t -> ph -> pw
    for ( uint32_t gh = 0; gh < grid_h; gh++ )
    {
        for ( uint32_t gw = 0; gw < grid_w; gw++ )
        {
            for ( uint32_t c = 0; c < channel; c++ )
            {
                for ( uint32_t t = 0; t < m_temporalPatchSize; t++ )
                {
                    for ( uint32_t ph = 0; ph < m_patchSize; ph++ )
                    {
                        for ( uint32_t pw = 0; pw < m_patchSize; pw++ )
                        {
                            // t does not shift position for single-frame input
                            uint32_t h       = gh * m_patchSize + ph;
                            uint32_t w       = gw * m_patchSize + pw;
                            uint32_t rgb_idx = ( h * width + w ) * 3 + c;

                            float pixel      = static_cast<float>( rgb_data[rgb_idx] ) / 255.0f;
                            float normalized = ( pixel - m_imageMean[c] ) / m_imageStd[c];
                            float quantized  = std::round( normalized / m_pixelValuesScale -
                                                           m_pixelValuesOffset );
                            quantized        = std::max( 0.0f, std::min( 65535.0f, quantized ) );
                            output[out_idx++] = static_cast<uint16_t>( quantized );
                        }
                    }
                }
            }
        }
    }
}

QCStatus_e SamplePreProcVisionEncoder::PreprocessCPU( DataFrames_t &frames )
{
    QCStatus_e ret = QC_STATUS_OK;

    QCBufferDescriptorBase_t &inputBufDesc = frames.frames[0].GetBuffer();
    const uint8_t *rgb_data = static_cast<const uint8_t *>( inputBufDesc.GetDataPtr() );
    if ( nullptr == rgb_data )
    {
        QC_ERROR( "Failed to get RGB data pointer\n" );
        return QC_STATUS_INVALID_BUF;
    }

    std::shared_ptr<SharedBuffer_t> tensorBuffer = m_tensorPool.Get();
    if ( nullptr == tensorBuffer )
    {
        QC_ERROR( "Failed to get tensor buffer\n" );
        return QC_STATUS_NOMEM;
    }

    uint16_t *output_data = static_cast<uint16_t *>( tensorBuffer->GetBuffer().GetDataPtr() );
    if ( nullptr == output_data )
    {
        QC_ERROR( "Tensor buffer data pointer is null\n" );
        return QC_STATUS_INVALID_BUF;
    }

    PreprocessRGBToUint16( rgb_data, m_resizedHeight, m_resizedWidth, output_data );

    DataFrames_t outFrames;
    DataFrame_t  frame;
    frame.buffer    = tensorBuffer;
    frame.frameId   = frames.FrameId( 0 );
    frame.timestamp = frames.Timestamp( 0 );
    outFrames.Add( frame );

    ret = m_pub.Publish( outFrames );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to publish frameId %" PRIu64 ": %d\n", frames.FrameId( 0 ), ret );
    }

    return ret;
}

// =============================================================================
// GPU path implementation
// =============================================================================

QCStatus_e SamplePreProcVisionEncoder::PreprocessGPU( DataFrames_t &frames )
{
    QCStatus_e ret = QC_STATUS_OK;

    std::shared_ptr<SharedBuffer_t> tensorBuffer = m_tensorPool.Get();
    if ( nullptr == tensorBuffer )
    {
        QC_ERROR( "Failed to get tensor buffer\n" );
        return QC_STATUS_NOMEM;
    }

    // Register input RGB buffer with OpenCL (cached internally; no duplicate registration)
    QCBufferDescriptorBase_t &inputBufDesc = frames.frames[0].GetBuffer();
    cl_mem inputCL = nullptr;
    ret = m_openclSrv.RegBufferDesc( inputBufDesc, inputCL );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to register input buffer to OpenCL\n" );
        return ret;
    }

    // Register output tensor buffer with OpenCL
    QCBufferDescriptorBase_t &outputBufDesc = tensorBuffer->GetBuffer();
    cl_mem outputCL = nullptr;
    ret = m_openclSrv.RegBufferDesc( outputBufDesc, outputCL );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to register output buffer to OpenCL\n" );
        return ret;
    }

    // Kernel arguments (order matches kernel declaration)
    const cl_uint  cl_width               = static_cast<cl_uint>( m_resizedWidth );
    const cl_uint  cl_grid_h              = static_cast<cl_uint>( m_gridH );
    const cl_uint  cl_grid_w              = static_cast<cl_uint>( m_gridW );
    const cl_uint  cl_patch_size          = static_cast<cl_uint>( m_patchSize );
    const cl_uint  cl_temporal_patch_size = static_cast<cl_uint>( m_temporalPatchSize );
    const cl_float cl_mean_r              = static_cast<cl_float>( m_imageMean[0] );
    const cl_float cl_mean_g              = static_cast<cl_float>( m_imageMean[1] );
    const cl_float cl_mean_b              = static_cast<cl_float>( m_imageMean[2] );
    const cl_float cl_std_r               = static_cast<cl_float>( m_imageStd[0] );
    const cl_float cl_std_g               = static_cast<cl_float>( m_imageStd[1] );
    const cl_float cl_std_b               = static_cast<cl_float>( m_imageStd[2] );
    const cl_float cl_scale               = static_cast<cl_float>( m_pixelValuesScale );
    const cl_int   cl_offset_val          = static_cast<cl_int>( m_pixelValuesOffset );

    OpenclIfcae_Arg_t args[] = {
        { &inputCL,                        sizeof( cl_mem )   },
        { &outputCL,                       sizeof( cl_mem )   },
        { (void *)&cl_width,               sizeof( cl_uint )  },
        { (void *)&cl_grid_h,              sizeof( cl_uint )  },
        { (void *)&cl_grid_w,              sizeof( cl_uint )  },
        { (void *)&cl_patch_size,          sizeof( cl_uint )  },
        { (void *)&cl_temporal_patch_size, sizeof( cl_uint )  },
        { (void *)&cl_mean_r,              sizeof( cl_float ) },
        { (void *)&cl_mean_g,              sizeof( cl_float ) },
        { (void *)&cl_mean_b,              sizeof( cl_float ) },
        { (void *)&cl_std_r,               sizeof( cl_float ) },
        { (void *)&cl_std_g,               sizeof( cl_float ) },
        { (void *)&cl_std_b,               sizeof( cl_float ) },
        { (void *)&cl_scale,               sizeof( cl_float ) },
        { (void *)&cl_offset_val,          sizeof( cl_int )   },
    };

    const size_t total_elements = static_cast<size_t>( m_numPatches ) * m_patchDim;
    static constexpr size_t LOCAL_WORK_SIZE = 256;
    size_t gws = ( ( total_elements + LOCAL_WORK_SIZE - 1 ) / LOCAL_WORK_SIZE ) * LOCAL_WORK_SIZE;
    size_t lws = LOCAL_WORK_SIZE;
    OpenclIface_WorkParams_t workParams = { nullptr, &gws, &lws, 1 };

    ret = m_openclSrv.Execute( &m_kernel, args, sizeof( args ) / sizeof( args[0] ), &workParams );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "OpenCL kernel execution failed for frameId %" PRIu64 ": %d\n",
                  frames.FrameId( 0 ), ret );
        return ret;
    }

    DataFrames_t outFrames;
    DataFrame_t  frame;
    frame.buffer    = tensorBuffer;
    frame.frameId   = frames.FrameId( 0 );
    frame.timestamp = frames.Timestamp( 0 );
    outFrames.Add( frame );

    ret = m_pub.Publish( outFrames );
    if ( QC_STATUS_OK != ret )
    {
        QC_ERROR( "Failed to publish frameId %" PRIu64 ": %d\n", frames.FrameId( 0 ), ret );
    }

    return ret;
}

// =============================================================================
// Thread main loop
// =============================================================================

void SamplePreProcVisionEncoder::ThreadMain()
{
    QCStatus_e ret;

    while ( false == m_stop )
    {
        DataFrames_t frames;
        ret = m_sub.Receive( frames );
        if ( QC_STATUS_OK != ret )
        {
            continue;
        }

        PROFILER_BEGIN();
        QC_TRACE_BEGIN( "Execute", { QCNodeTraceArg( "frameId", frames.FrameId( 0 ) ) } );
        QC_DEBUG( "receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", frames.FrameId( 0 ),
                  frames.Timestamp( 0 ) );

        if ( QC_PROCESSOR_CPU == m_processor )
        {
            ret = PreprocessCPU( frames );
        }
        else
        {
            ret = PreprocessGPU( frames );
        }

        if ( QC_STATUS_OK == ret )
        {
            PROFILER_END();
        }
        QC_TRACE_END( "Execute", { QCNodeTraceArg( "frameId", frames.FrameId( 0 ) ) } );
    }
}

QCStatus_e SamplePreProcVisionEncoder::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;

    QC_TRACE_BEGIN( "Stop", {} );
    m_stop = true;
    if ( m_thread.joinable() )
    {
        m_thread.join();
    }
    QC_TRACE_END( "Stop", {} );

    PROFILER_SHOW();

    return ret;
}

QCStatus_e SamplePreProcVisionEncoder::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;

    QC_TRACE_BEGIN( "Deinit", {} );
    if ( QC_PROCESSOR_GPU == m_processor )
    {
        QCStatus_e clRet = m_openclSrv.Deinit();
        if ( QC_STATUS_OK != clRet )
        {
            QC_ERROR( "Failed to deinit OpenCL: %d\n", clRet );
            ret = clRet;
        }
    }

    BufferManager::Put( m_pBufMgr );
    m_pBufMgr = nullptr;
    QC_TRACE_END( "Deinit", {} );

    return ret;
}

REGISTER_SAMPLE( PreProcVisionEncoder, SamplePreProcVisionEncoder );

}   // namespace sample
}   // namespace QC