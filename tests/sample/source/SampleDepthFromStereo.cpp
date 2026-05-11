// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/sample/SampleDepthFromStereo.hpp"

namespace QC
{
namespace sample
{

#define ALIGN_S( size, align ) ( ( size + align - 1 ) / align ) * align

SampleDepthFromStereo::SampleDepthFromStereo() {}
SampleDepthFromStereo::~SampleDepthFromStereo() {}

#ifdef QC_ENABLE_HS
std::function<void( const std::uint32_t *, std::size_t )>
SampleDepthFromStereo::GetRunnableCallback()
{
    m_bOrchestratorEnabled = true;
    return std::bind( &SampleDepthFromStereo::RunnableCallback, this, std::placeholders::_1,
                      std::placeholders::_2 );
}

void SampleDepthFromStereo::RunnableCallback( const std::uint32_t *rids, std::size_t count )
{
    Execute();
}
#endif


QCStatus_e SampleDepthFromStereo::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    m_config.Set<std::string>( "name", m_name );

    m_width = Get( config, "width", 1280 );
    m_config.Set<uint32_t>( "width", m_width );

    m_height = Get( config, "height", 416 );
    m_config.Set<uint32_t>( "height", m_height );

    m_config.Set<std::string>( "format", Get( config, "format", "nv12" ) );
    m_config.Set<uint32_t>( "fps", Get( config, "fps", 30 ) );
    m_config.Set<bool>( "confidenceOutputEn", Get( config, "confidence_output", true ) );
    m_config.Set<uint8_t>( "processingMode",
                           Get( config, "processing_mode",
                                static_cast<uint32_t>( PROCESSING_MODE_AUTO ) ) );

    std::string searchDirection = Get( config, "search_direction", "l2r" );
    if ( searchDirection == "l2r" )
    {
        m_config.Set<uint8_t>( "searchDirection", SEARCH_DIRECTION_L2R );
    }
    else if ( searchDirection == "r2l" )
    {
        m_config.Set<uint8_t>( "searchDirection", SEARCH_DIRECTION_R2L );
    }
    else
    {
        QC_ERROR( "invalid search_direction = %s\n", searchDirection.c_str() );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    bool bCache = Get( config, "cache", true );
    if ( false == bCache )
    {
        m_bufferCache = QC_CACHEABLE_NON;
    }
    else
    {
        m_bufferCache = QC_CACHEABLE;
    }

    m_poolSize = Get( config, "pool_size", 4 );
    m_config.Set<uint32_t>( "pool_size", m_poolSize );

    m_inputTopicName = Get( config, "input_topic", "" );
    if ( "" == m_inputTopicName )
    {
        QC_ERROR( "no input topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_outputTopicName = Get( config, "output_topic", "" );
    if ( "" == m_outputTopicName )
    {
        QC_ERROR( "no output topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_dataTree.Set( "static", m_config );

    return ret;
}

QCStatus_e SampleDepthFromStereo::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = SampleIF::Init( name );
    if ( QC_STATUS_OK == ret )
    {
        ret = ParseConfig( config );
    }

    uint32_t width = m_width;
    uint32_t height = m_height;

    if ( QC_STATUS_OK == ret )
    {
        TensorProps_t dispTsProp( QC_TENSOR_TYPE_UINT_16,
                                  { 1, ALIGN_S( height, 2 ), ALIGN_S( width, 128 ), 1 },
                                  QC_MEMORY_ALLOCATOR_DMA_EVA, m_bufferCache );

        ret = m_dispPool.Init( name + ".disp", m_nodeId, LOGGER_LEVEL_INFO, m_poolSize,
                               dispTsProp );
    }

    if ( QC_STATUS_OK == ret )
    {
        TensorProps_t confTsProp( QC_TENSOR_TYPE_UINT_8,
                                  { 1, ALIGN_S( height, 2 ), ALIGN_S( width, 128 ),
                                    QC_MEMORY_ALLOCATOR_DMA_EVA, m_bufferCache } );

        ret = m_confPool.Init( name + ".conf", m_nodeId, LOGGER_LEVEL_INFO, m_poolSize,
                               confTsProp );
    }

    if ( QC_STATUS_OK == ret )
    {
        using std::placeholders::_1;

        QCNodeInit_t config = { m_dataTree.Dump() };

        ret = m_dfs.Initialize( config );
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

QCStatus_e SampleDepthFromStereo::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    TRACE_BEGIN( SYSTRACE_TASK_START );
    ret = m_dfs.Start();
    TRACE_END( SYSTRACE_TASK_START );
    if ( QC_STATUS_OK == ret )
    {
        m_stop = false;
#ifdef QC_ENABLE_HS
        if ( !m_bOrchestratorEnabled )
        {
#endif
            m_thread = std::thread( &SampleDepthFromStereo::ThreadMain, this );
#ifdef QC_ENABLE_HS
        }
#endif
    }

    return ret;
}

void SampleDepthFromStereo::Execute()
{
    QCStatus_e ret;
    NodeFrameDescriptor frameDescriptor( QC_NODE_DFS_LAST_BUFF_ID );

    DataFrames_t frames;
    uint32_t timeout = 1000;
#ifdef QC_ENABLE_HS
    if ( m_bOrchestratorEnabled )
    {
        timeout = 0;
    }
#endif
    ret = m_sub.Receive( frames, timeout );
    if ( QC_STATUS_OK == ret )
    {
        if ( 2 != frames.frames.size() )
        {
            QC_ERROR( "DepthFromStereo expect 2 input images" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        QC_DEBUG( "receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", frames.FrameId( 0 ),
                  frames.Timestamp( 0 ) );
        std::shared_ptr<SharedBuffer_t> disp = m_dispPool.Get();
        std::shared_ptr<SharedBuffer_t> conf = m_confPool.Get();
        if ( ( nullptr != disp ) && ( nullptr != conf ) )
        {

            QCBufferDescriptorBase_t &buffPriImg = frames.GetBuffer( 0 );
            QCBufferDescriptorBase_t &buffAuxImg = frames.GetBuffer( 1 );
            QCBufferDescriptorBase_t &buffDispMap = disp->buffer;
            QCBufferDescriptorBase_t &buffConfMap = conf->buffer;


            ImageDescriptor_t &buffPriImgDesc = dynamic_cast<ImageDescriptor_t &>( buffPriImg );
            ImageDescriptor_t &buffAuxImgDesc = dynamic_cast<ImageDescriptor_t &>( buffAuxImg );
            TensorDescriptor_t &buffDispMapDesc = dynamic_cast<TensorDescriptor_t &>( buffDispMap );
            TensorDescriptor_t &buffConfMapDesc = dynamic_cast<TensorDescriptor_t &>( buffConfMap );

            PROFILER_BEGIN();
            TRACE_BEGIN( frames.FrameId( 0 ) );

            QCStatus_e status = frameDescriptor.SetBuffer(
                    static_cast<uint32_t>( QC_NODE_DFS_PRIMARY_IMAGE_BUFF_ID ), buffPriImgDesc );
            status = frameDescriptor.SetBuffer(
                    static_cast<uint32_t>( QC_NODE_DFS_AUXILARY_IMAGE_BUFF_ID ), buffAuxImgDesc );
            status = frameDescriptor.SetBuffer(
                    static_cast<uint32_t>( QC_NODE_DFS_DISPARITY_MAP_BUFF_ID ), buffDispMapDesc );
            status = frameDescriptor.SetBuffer(
                    static_cast<uint32_t>( QC_NODE_DFS_DISPARITY_CONFIDANCE_MAP_BUFF_ID ),
                    buffConfMapDesc );

            ret = m_dfs.ProcessFrameDescriptor( frameDescriptor );

            if ( QC_STATUS_OK == ret )
            {
                PROFILER_END();
                TRACE_END( frames.FrameId( 0 ) );
                DataFrames_t outFrames;
                DataFrame_t frame;
                frame.buffer = disp;
                frame.frameId = frames.FrameId( 0 );
                frame.timestamp = frames.Timestamp( 0 );
                outFrames.Add( frame );
                frame.buffer = conf;
                outFrames.Add( frame );
                m_pub.Publish( outFrames );
            }
            else
            {
                QC_ERROR( "DepthFromStereo failed for %" PRIu64 " : %d", frames.FrameId( 0 ), ret );
            }
        }
    }
#ifdef QC_ENABLE_HS
    else if ( m_bOrchestratorEnabled )
    {
        QC_ERROR( "DepthFromStereo receive failed : %d", ret );
    }
#endif
}

void SampleDepthFromStereo::ThreadMain()
{
    while ( false == m_stop )
    {
        Execute();
    }
}

QCStatus_e SampleDepthFromStereo::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;

    m_stop = true;
#ifdef QC_ENABLE_HS
    // Join thread only if orchestrator is NOT enabled
    if ( !m_bOrchestratorEnabled )
    {
#endif
        if ( m_thread.joinable() )
        {
            m_thread.join();
        }
#ifdef QC_ENABLE_HS
    }
#endif

    PROFILER_SHOW();

    TRACE_BEGIN( SYSTRACE_TASK_STOP );
    ret = m_dfs.Stop();
    TRACE_END( SYSTRACE_TASK_STOP );


    return ret;
}

QCStatus_e SampleDepthFromStereo::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;

    TRACE_BEGIN( SYSTRACE_TASK_DEINIT );
    ret = m_dfs.DeInitialize();
    TRACE_END( SYSTRACE_TASK_DEINIT );

    return ret;
}

const uint32_t SampleDepthFromStereo::GetVersion() const
{
    return QCNODE_DFS_VERSION;
}

REGISTER_SAMPLE( DepthFromStereo, SampleDepthFromStereo );

}   // namespace sample
}   // namespace QC
