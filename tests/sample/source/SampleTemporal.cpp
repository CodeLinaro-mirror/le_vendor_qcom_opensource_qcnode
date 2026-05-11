// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/sample/SampleTemporal.hpp"
#include <algorithm>
#include <cmath>
#include <math.h>

namespace QC
{
namespace sample
{

SampleTemporal::SampleTemporal() {}
SampleTemporal::~SampleTemporal() {}

#ifdef QC_ENABLE_HS
std::function<void( const std::uint32_t *, std::size_t )> SampleTemporal::GetRunnableCallback()
{
    m_bOrchestratorEnabled = true;
    return std::bind( &SampleTemporal::RunnableCallback, this, std::placeholders::_1,
                      std::placeholders::_2 );
}

void SampleTemporal::RunnableCallback( const std::uint32_t *rids, std::size_t count )
{
    Execute();
}
#endif

QCStatus_e SampleTemporal::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

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

    m_number = Get( config, "number", 1 );
    m_temporal.resize( m_number );

    QCTensorType_e tensorTypeDft =
            Get( config, "temporal_tensor_type", QC_TENSOR_TYPE_UFIXED_POINT_8 );
    std::vector<uint32_t> dimsTempDft;
    dimsTempDft = Get( config, "temporal_tensor_dims", dimsTempDft );
    float temporalQuantScaleDft = Get( config, "temporal_quant_scale", 1.0f );
    int32_t temporalQuantOffsetDft = Get( config, "temporal_quant_offset", 0 );
    uint32_t temporalIndexDft = Get( config, "temporal_index", 0u );

    for ( uint32_t i = 0; i < m_number; i++ )
    {
        std::string suffix = std::to_string( i );
        m_temporal[i].temporalTsProps.tensorType =
                Get( config, "temporal_tensor_type" + suffix, tensorTypeDft );
        if ( QC_TENSOR_TYPE_MAX == m_temporal[i].temporalTsProps.tensorType )
        {
            QC_ERROR( "invalid temporal_tensor_type\n" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        std::vector<uint32_t> dimsTemp;
        dimsTemp = Get( config, "temporal_tensor_dims" + suffix, dimsTempDft );
        if ( 0 == dimsTemp.size() )
        {
            QC_ERROR( "invalid temporal_tensor_dims\n" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        m_temporal[i].temporalTsProps.numDims = dimsTemp.size();
        for ( size_t j = 0; j < dimsTemp.size(); j++ )
        {
            m_temporal[i].temporalTsProps.dims[j] = dimsTemp[j];
        }
        m_temporal[i].temporalQuantScale =
                Get( config, "temporal_quant_scale" + suffix, temporalQuantScaleDft );
        m_temporal[i].temporalQuantOffset =
                Get( config, "temporal_quant_offset" + suffix, temporalQuantOffsetDft );
        m_temporal[i].temporalIndex = Get( config, "temporal_index" + suffix, temporalIndexDft );
    }

    m_useFlagTsProps.tensorType = Get( config, "use_flag_tensor_type", QC_TENSOR_TYPE_MAX );
    if ( QC_TENSOR_TYPE_MAX == m_useFlagTsProps.tensorType )
    {
        QC_INFO( "temporal use flag was not used\n" );
        m_bHasUseFlag = false;
    }
    else
    {
        m_bHasUseFlag = true;
        std::vector<uint32_t> dimsFlag = { 1 };
        dimsFlag = Get( config, "use_flag_tensor_dims", dimsFlag );
        if ( 0 == dimsFlag.size() )
        {
            QC_ERROR( "invalid use_flag_tensor_dims\n" );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        m_useFlagTsProps.numDims = dimsFlag.size();
        for ( size_t i = 0; i < dimsFlag.size(); i++ )
        {
            m_useFlagTsProps.dims[i] = dimsFlag[i];
        }
        m_useFlagQuantScale = Get( config, "use_flag_quant_scale", 1.0f );
        m_useFlagQuantOffset = Get( config, "use_flag_quant_offset", 0 );
    }

    m_windowMs = Get( config, "window", 200u );

    return ret;
}

QCStatus_e SampleTemporal::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = SampleIF::Init( name );
    if ( QC_STATUS_OK == ret )
    {
        ret = ParseConfig( config );
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
            QC_ERROR( "Failed to create buffer manager!" );
            ret = QC_STATUS_NOMEM;
        }
    }

    for ( uint32_t i = 0; ( i < m_number ) && ( QC_STATUS_OK == ret ); i++ )
    {
        ret = m_pBufMgr->Allocate( m_temporal[i].temporalTsProps, m_temporal[i].initTempTs );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "Failed to allocal temporal init tensor!" );
            ret = QC_STATUS_NOMEM;
        }
        else
        {
            m_temporal[i].temporal = std::make_shared<SharedBuffer_t>();
            m_temporal[i].temporal->SetBuffer( m_temporal[i].initTempTs );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        if ( true == m_bHasUseFlag )
        {
            ret = m_pBufMgr->Allocate( m_useFlagTsProps, m_useFlagTs );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Failed to allocal use flag tensor!" );
                ret = QC_STATUS_NOMEM;
            }
            else
            {
                m_useFlag = std::make_shared<SharedBuffer_t>();
                m_useFlag->SetBuffer( m_useFlagTs );

                ret = FillTensor( m_useFlagTs, m_useFlagQuantScale, m_useFlagQuantOffset, 0.0f );
            }
        }
        else
        { /* if without use flag tensor, init temporal with 0 */
            for ( uint32_t i = 0; ( i < m_number ) && ( QC_STATUS_OK == ret ); i++ )
            {
                ret = FillTensor( m_temporal[i].initTempTs, m_temporal[i].temporalQuantScale,
                                  m_temporal[i].temporalQuantOffset, 0.0f );
            }
        }
    }

    return ret;
}

QCStatus_e SampleTemporal::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    m_stop = false;
    m_frameId = 0;

    { /* publish the 1st frame */
        DataFrames_t frames;
        for ( uint32_t i = 0; i < m_number; i++ )
        {
            DataFrame_t frame;
            frame.buffer = m_temporal[i].temporal;
            frame.frameId = m_frameId;
            frames.Add( frame );
        }
        if ( true == m_bHasUseFlag )
        {
            (void) FillTensor( m_useFlagTs, m_useFlagQuantScale, m_useFlagQuantOffset, 0.0f );
            DataFrame_t frame;
            frame.buffer = m_useFlag;
            frame.frameId = m_frameId;
            frames.Add( frame );
        }
        m_frameId++;
        m_pub.Publish( frames );
    }

#ifdef QC_ENABLE_HS
    if ( !m_bOrchestratorEnabled )
    {
#endif
        m_thread = std::thread( &SampleTemporal::ThreadMain, this );
#ifdef QC_ENABLE_HS
    }
#endif

    return ret;
}

QCStatus_e SampleTemporal::FillTensor( TensorDescriptor_t &tensorDesc, float scale, int32_t offset,
                                       float value )
{
    QCStatus_e ret = QC_STATUS_OK;

    switch ( tensorDesc.tensorType )
    {
        case QC_TENSOR_TYPE_FLOAT_32:
        {
            float *pF32 = static_cast<float *>( tensorDesc.GetDataPtr() );
            uint32_t num = tensorDesc.GetDataSize() / sizeof( float );
            std::fill( pF32, pF32 + num, value );
            break;
        }
        case QC_TENSOR_TYPE_UFIXED_POINT_8:
        {
            uint8_t *pU8 = static_cast<uint8_t *>( tensorDesc.GetDataPtr() );
            uint32_t num = tensorDesc.GetDataSize() / sizeof( uint8_t );
            uint8_t quantU8 = (uint8_t) std::min(
                    std::max( std::round( value / scale - offset ), 0.0f ), 255.0f );
            std::fill( pU8, pU8 + num, quantU8 );
            break;
        }
        case QC_TENSOR_TYPE_UFIXED_POINT_16:
        {
            uint16_t *pU16 = static_cast<uint16_t *>( tensorDesc.GetDataPtr() );
            uint32_t num = tensorDesc.GetDataSize() / sizeof( uint16_t );
            uint16_t quantU16 = (uint16_t) std::min(
                    std::max( std::round( value / scale - offset ), 0.0f ), 65535.0f );
            std::fill( pU16, pU16 + num, quantU16 );
            break;
        }
        default:
            QC_ERROR( "tensor with type %d is not supported", tensorDesc.tensorType );
            ret = QC_STATUS_BAD_ARGUMENTS;
            break;
    }

    return ret;
}

void SampleTemporal::Execute()
{
    QCStatus_e ret;
    uint64_t timeoutMs = (uint64_t) m_windowMs;
#ifdef QC_ENABLE_HS
    if ( m_bOrchestratorEnabled )
    {
        timeoutMs = 0;
    }
#endif
    DataFrames_t frames;
    ret = m_sub.Receive( frames, timeoutMs );
    if ( QC_STATUS_OK == ret )
    {
        QC_DEBUG( "receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", frames.FrameId( 0 ),
                  frames.Timestamp( 0 ) );
        for ( uint32_t i = 0; i < m_number; i++ )
        {
            if ( m_temporal[i].temporalIndex < frames.frames.size() )
            {
                m_temporal[i].temporal = frames.frames[m_temporal[i].temporalIndex].buffer;
            }
            else
            {
                QC_ERROR( "temporal[%u] index %u out of range.", i, m_temporal[i].temporalIndex );
                ret = QC_STATUS_FAIL;
            }
        }
        if ( QC_STATUS_OK != ret )
        {
            m_stop = true; /* exit as wrong configuration */
            return;
        }
    }
    else
    {
        QC_WARN( "reach deadline, publish history data instead." );
    }
    DataFrames_t framesOut;
    for ( uint32_t i = 0; i < m_number; i++ )
    {
        DataFrame_t frame;
        frame.buffer = m_temporal[i].temporal;
        frame.frameId = m_frameId;
        framesOut.Add( frame );
    }
    if ( true == m_bHasUseFlag )
    {
        (void) FillTensor( m_useFlagTs, m_useFlagQuantScale, m_useFlagQuantOffset, 1.0f );
        DataFrame_t frame;
        frame.buffer = m_useFlag;
        frame.frameId = m_frameId;
        framesOut.Add( frame );
    }
    m_frameId++;
    m_pub.Publish( framesOut );
}

void SampleTemporal::ThreadMain()
{
    while ( false == m_stop )
    {
        Execute();
    }
}

QCStatus_e SampleTemporal::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;

    m_stop = true;
#ifdef QC_ENABLE_HS
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

    return ret;
}

QCStatus_e SampleTemporal::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;
    if ( nullptr != m_pBufMgr )
    {
        BufferManager::Put( m_pBufMgr );
        m_pBufMgr = nullptr;
    }
    return ret;
}

REGISTER_SAMPLE( Temporal, SampleTemporal );

}   // namespace sample
}   // namespace QC
