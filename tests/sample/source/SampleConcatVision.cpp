// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/sample/SampleConcatVision.hpp"

#include <cstring>
#include <fstream>

namespace QC
{
namespace sample
{

SampleConcatVision::SampleConcatVision() {}
SampleConcatVision::~SampleConcatVision() {}

QCStatus_e SampleConcatVision::LoadInputIds( const std::string &filepath )
{
    QCStatus_e ret = QC_STATUS_OK;

    std::ifstream file( filepath, std::ios::binary | std::ios::ate );
    if ( !file )
    {
        QC_ERROR( "Failed to open input_ids file: %s\n", filepath.c_str() );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    std::streamsize file_size = 0;
    if ( QC_STATUS_OK == ret )
    {
        file_size = file.tellg();
        if ( file_size <= 0 )
        {
            QC_ERROR( "input_ids file is empty: %s\n", filepath.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( file_size % static_cast<std::streamsize>( sizeof( int64_t ) ) != 0 )
        {
            QC_ERROR( "input_ids file size is not a multiple of int64_t: %s\n",
                      filepath.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        const size_t num_elements = static_cast<size_t>( file_size / sizeof( int64_t ) );
        m_inputIds.resize( num_elements );
        file.seekg( 0, std::ios::beg );
        if ( !file.read( reinterpret_cast<char *>( m_inputIds.data() ), file_size ) )
        {
            QC_ERROR( "Failed to read input_ids from: %s\n", filepath.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        m_inputIdsBatchSize = 1;
        m_inputIdsSeqLen    = static_cast<int>( m_inputIds.size() );
    }

    return ret;
}

QCStatus_e SampleConcatVision::LoadInputEmbeds( const std::string &filepath )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( m_inputIdsBatchSize == 0 || m_inputIdsSeqLen == 0 )
    {
        QC_ERROR( "LoadInputIds must be called before LoadInputEmbeds\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    std::ifstream file;
    if ( QC_STATUS_OK == ret )
    {
        file.open( filepath, std::ios::binary );
        if ( !file )
        {
            QC_ERROR( "Failed to open input_embeds file: %s\n", filepath.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    size_t num_elements = 0;
    if ( QC_STATUS_OK == ret )
    {
        file.seekg( 0, std::ios::end );
        size_t file_size = file.tellg();
        file.seekg( 0, std::ios::beg );

        num_elements = file_size / sizeof( float );
        m_inputEmbeds.resize( num_elements );
        file.read( reinterpret_cast<char *>( m_inputEmbeds.data() ), file_size );
    }

    if ( QC_STATUS_OK == ret )
    {
        int expected_hidden_dim =
                static_cast<int>( num_elements ) / ( m_inputIdsBatchSize * m_inputIdsSeqLen );
        if ( num_elements !=
             static_cast<size_t>( m_inputIdsBatchSize * m_inputIdsSeqLen * expected_hidden_dim ) )
        {
            QC_ERROR( "input_embeds file size does not match expected dimensions: %s\n",
                      filepath.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_inputEmbedsDim = expected_hidden_dim;
        }
    }

    return ret;
}

QCStatus_e SampleConcatVision::Execute( float *input, float *output )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( m_inputIds.empty() || m_inputEmbeds.empty() )
    {
        QC_ERROR( "input_ids or input_embeds not loaded\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( input == nullptr )
        {
            QC_ERROR( "VIT embeddings pointer is null\n" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    const int B = m_inputIdsBatchSize;
    const int N = m_inputIdsSeqLen;
    const int C = m_inputEmbedsDim;

    if ( QC_STATUS_OK == ret )
    {
        if ( m_inputIds.size() != static_cast<size_t>( B * N ) )
        {
            QC_ERROR( "input_ids size mismatch: expected %d, got %zu\n", B * N,
                      m_inputIds.size() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( m_inputEmbeds.size() != static_cast<size_t>( B * N * C ) )
        {
            QC_ERROR( "input_embeds size mismatch: expected %d, got %zu\n", B * N * C,
                      m_inputEmbeds.size() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( m_vitHiddenDim != C )
        {
            QC_ERROR( "VIT hidden_dim %d does not match input_embeds hidden_dim %d\n",
                      m_vitHiddenDim, C );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    int image_token_count = 0;
    if ( QC_STATUS_OK == ret )
    {
        for ( int64_t id : m_inputIds )
        {
            if ( id == m_imageTokenIndex )
            {
                ++image_token_count;
            }
        }
        if ( image_token_count > m_vitSeqLen )
        {
            QC_ERROR( "More image tokens (%d) than available VIT embeddings (%d)\n",
                      image_token_count, m_vitSeqLen );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        // Write directly to output: use VIT embedding for image tokens, text embedding otherwise.
        // This avoids the intermediate copy of m_inputEmbeds into a temporary vector.
        int vit_index = 0;
        for ( int i = 0; i < B * N; ++i )
        {
            if ( m_inputIds[i] == m_imageTokenIndex )
            {
                if ( vit_index >= m_vitSeqLen )
                {
                    QC_ERROR( "VIT embedding index out of range\n" );
                    ret = QC_STATUS_BAD_ARGUMENTS;
                    break;
                }
                std::memcpy( &output[i * C], &input[vit_index * C], C * sizeof( float ) );
                ++vit_index;
            }
            else
            {
                std::memcpy( &output[i * C], &m_inputEmbeds[i * C], C * sizeof( float ) );
            }
        }
    }

    return ret;
}

QCStatus_e SampleConcatVision::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

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

    m_poolSize        = Get( config, "pool_size", 4 );
    m_imageTokenIndex = Get( config, "image_token_index", 151655 );

    m_vitSeqLen    = Get( config, "vit_seq_len", 529 );
    m_vitHiddenDim = Get( config, "vit_hidden_dim", 2048 );

    const std::string input_ids_path =
            Get( config, "input_ids_file", "/data/input_ids.raw" );
    const std::string input_embeds_path =
            Get( config, "input_embeds_file", "/data/input_embeds_before_replace.raw" );

    if ( QC_STATUS_OK == ret && !input_ids_path.empty() )
    {
        ret = LoadInputIds( input_ids_path );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Failed to load input_ids\n" );
        }
    }

    if ( QC_STATUS_OK == ret && !input_embeds_path.empty() )
    {
        ret = LoadInputEmbeds( input_embeds_path );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Failed to load input_embeds\n" );
        }
    }

    // Derive output tensor shape from loaded data: [batch, seq_len, hidden_dim]
    if ( QC_STATUS_OK == ret )
    {
        m_embdesTsProps.tensorType = QC_TENSOR_TYPE_FLOAT_32;
        m_embdesTsProps.numDims    = 3;
        m_embdesTsProps.dims[0]    = static_cast<uint32_t>( m_inputIdsBatchSize );
        m_embdesTsProps.dims[1]    = static_cast<uint32_t>( m_inputIdsSeqLen );
        m_embdesTsProps.dims[2]    = static_cast<uint32_t>( m_inputEmbedsDim );
    }

    return ret;
}

QCStatus_e SampleConcatVision::Init( std::string name, SampleConfig_t &config )
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
            dt.Set<std::string>( "processor", "cpu" );
            return dt.Dump();
        }() );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_sub.Init( name, m_inputTopicName );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_pub.Init( name, m_outputTopicName );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_pBufMgr = BufferManager::Get( m_nodeId, m_logger.GetLevel() );
        if ( nullptr == m_pBufMgr )
        {
            QC_ERROR( "Failed to create buffer manager!\n" );
            ret = QC_STATUS_NOMEM;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_embdesPool.Init( name + ".embeds", m_nodeId, LOGGER_LEVEL_INFO, m_poolSize,
                                 m_embdesTsProps );
    }

    return ret;
}

QCStatus_e SampleConcatVision::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    QC_TRACE_BEGIN( "Start", {} );
    m_stop   = false;
    m_thread = std::thread( &SampleConcatVision::ThreadMain, this );
    QC_TRACE_END( "Start", {} );

    return ret;
}

void SampleConcatVision::ThreadMain()
{
    QCStatus_e ret;

    while ( false == m_stop )
    {
        DataFrames_t framesIn;
        ret = m_sub.Receive( framesIn );
        if ( QC_STATUS_OK != ret )
        {
            continue;
        }

        PROFILER_BEGIN();
        QC_TRACE_BEGIN( "Execute", { QCNodeTraceArg( "frameId", framesIn.FrameId( 0 ) ) } );
        QC_DEBUG( "receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", framesIn.FrameId( 0 ),
                  framesIn.Timestamp( 0 ) );

        std::shared_ptr<SharedBuffer_t> embds = m_embdesPool.Get();
        if ( nullptr == embds )
        {
            QC_ERROR( "Failed to get output buffer from pool\n" );
            QC_TRACE_END( "Execute", { QCNodeTraceArg( "frameId", framesIn.FrameId( 0 ) ) } );
            continue;
        }

        // VIT embeddings are already float32, use directly
        QCBufferDescriptorBase_t &vitBufDesc = framesIn.GetBuffer( 0 );
        float *vitFloat = reinterpret_cast<float *>( vitBufDesc.GetDataPtr() );
        if ( nullptr == vitFloat )
        {
            QC_ERROR( "Failed to get VIT embeddings pointer\n" );
            QC_TRACE_END( "Execute", { QCNodeTraceArg( "frameId", framesIn.FrameId( 0 ) ) } );
            continue;
        }

        float *outPtr = static_cast<float *>( embds->GetBuffer().GetDataPtr() );
        ret           = Execute( vitFloat, outPtr );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Execute failed for frameId %" PRIu64 ": %d\n", framesIn.FrameId( 0 ),
                      ret );
            QC_TRACE_END( "Execute", { QCNodeTraceArg( "frameId", framesIn.FrameId( 0 ) ) } );
            continue;
        }

        DataFrames_t framesOut;
        DataFrame_t  frame;
        frame.buffer  = embds;
        frame.frameId = framesIn.FrameId( 0 );
        framesOut.Add( frame );
        m_pub.Publish( framesOut );

        PROFILER_END();
        QC_TRACE_END( "Execute", { QCNodeTraceArg( "frameId", framesIn.FrameId( 0 ) ) } );
    }
}

QCStatus_e SampleConcatVision::Stop()
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

QCStatus_e SampleConcatVision::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;

    QC_TRACE_BEGIN( "Deinit", {} );
    if ( nullptr != m_pBufMgr )
    {
        BufferManager::Put( m_pBufMgr );
        m_pBufMgr = nullptr;
    }
    QC_TRACE_END( "Deinit", {} );

    return ret;
}

REGISTER_SAMPLE( ConcatVision, SampleConcatVision );

}   // namespace sample
}   // namespace QC
