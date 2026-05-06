// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "QC/sample/SampleDataReader.hpp"
#include <chrono>
#include <time.h>

namespace QC
{
namespace sample
{

static std::string s_qcFormatToStr[QC_IMAGE_FORMAT_MAX] = {
        ".rgb",       /* QC_IMAGE_FORMAT_RGB888 */
        ".bgr",       /* QC_IMAGE_FORMAT_BGR888 */
        ".uyvy",      /* QC_IMAGE_FORMAT_UYVY */
        ".nv12",      /* QC_IMAGE_FORMAT_NV12 */
        ".p010",      /* QC_IMAGE_FORMAT_P010 */
        ".nv12_ubwc", /* QC_IMAGE_FORMAT_NV12_UBWC */
        ".tp10_ubwc", /* QC_IMAGE_FORMAT_TP10_UBWC */
};

#define SIZE_OF_FLOAT16 2
static uint32_t s_qcTensorTypeToDataSize[QC_TENSOR_TYPE_MAX] = {
        sizeof( int8_t ),  /* QC_TENSOR_TYPE_INT_8 */
        sizeof( int16_t ), /* QC_TENSOR_TYPE_INT_16 */
        sizeof( int32_t ), /* QC_TENSOR_TYPE_INT_32 */
        sizeof( int64_t ), /* QC_TENSOR_TYPE_INT_64 */

        sizeof( uint8_t ),  /* QC_TENSOR_TYPE_UINT_8 */
        sizeof( uint16_t ), /* QC_TENSOR_TYPE_UINT_16 */
        sizeof( uint32_t ), /* QC_TENSOR_TYPE_UINT_32 */
        sizeof( uint64_t ), /* QC_TENSOR_TYPE_UINT_64 */

        SIZE_OF_FLOAT16,  /* QC_TENSOR_TYPE_FLOAT_16 */
        sizeof( float ),  /* QC_TENSOR_TYPE_FLOAT_32 */
        sizeof( double ), /* QC_TENSOR_TYPE_FLOAT_64 */

        sizeof( int8_t ),  /* QC_TENSOR_TYPE_SFIXED_POINT_8 */
        sizeof( int16_t ), /* QC_TENSOR_TYPE_SFIXED_POINT_16 */
        sizeof( int32_t ), /* QC_TENSOR_TYPE_SFIXED_POINT_32 */

        sizeof( uint8_t ),  /* QC_TENSOR_TYPE_UFIXED_POINT_8 */
        sizeof( uint16_t ), /* QC_TENSOR_TYPE_UFIXED_POINT_16 */
        sizeof( uint32_t )  /* QC_TENSOR_TYPE_UFIXED_POINT_32 */
};

SampleDataReader::SampleDataReader() {}
SampleDataReader::~SampleDataReader() {}

#ifdef QC_ENABLE_HS
std::function<void( const std::uint32_t *, std::size_t )> SampleDataReader::GetRunnableCallback()
{
    std::function<void( const std::uint32_t *, std::size_t )> callback = nullptr;
    m_bOrchestratorEnabled = true;
    if ( m_bEnableHsSleep )
    {
        m_lastFrameTime = std::chrono::high_resolution_clock::now();
        callback = std::bind( &SampleDataReader::RunnableCallbackWithSleep, this,
                              std::placeholders::_1, std::placeholders::_2 );
    }
    else
    {
        callback = std::bind( &SampleDataReader::RunnableCallback, this, std::placeholders::_1,
                              std::placeholders::_2 );
    }

    return callback;
}

void SampleDataReader::RunnableCallback( const std::uint32_t *rids, std::size_t count )
{
    Execute();
}

void SampleDataReader::RunnableCallbackWithSleep( const std::uint32_t *rids, std::size_t count )
{
    // Simulate camera FPS: enforce frame interval relative to when the last Execute() completed
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>( now - m_lastFrameTime ).count();

    if ( m_frameIntervalNs > elapsedNs )
    {
        std::this_thread::sleep_for( std::chrono::nanoseconds( m_frameIntervalNs - elapsedNs ) );
    }
    else if ( 0 == m_frameIntervalNs )
    {
        /* first run here, calculate m_frameIntervalNs */
        m_frameIntervalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::seconds{ 1 } )
                        .count() /
                m_fps;
    }
    else
    {
        /* do nothing */
    }

    Execute();

    // Record timestamp after Execute() completes; used next invocation to enforce FPS interval
    m_lastFrameTime = std::chrono::high_resolution_clock::now();
}
#endif

QCStatus_e SampleDataReader::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    m_numOfDataReaders = Get( config, "number", 1 );
    if ( 0 == m_numOfDataReaders )
    {
        QC_ERROR( "invalid number\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        m_configs.reserve( m_numOfDataReaders );
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

    for ( uint32_t i = 0; ( i < m_numOfDataReaders ) && ( QC_STATUS_OK == ret ); i++ )
    {
        DataReaderConfig_t cfg;

        auto typeStr = Get( config, "type" + std::to_string( i ), "image" );

        if ( "image" == typeStr )
        {
            cfg.type = DATA_READER_TYPE_IMAGE;
            cfg.format = Get( config, "format" + std::to_string( i ), QC_IMAGE_FORMAT_NV12 );
            if ( QC_IMAGE_FORMAT_MAX == cfg.format )
            {
                QC_ERROR( "invalid format%u\n", i );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }

            cfg.width = Get( config, "width" + std::to_string( i ), 1920 );
            if ( 0 == cfg.width )
            {
                QC_ERROR( "invalid width%u\n", i );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }

            cfg.height = Get( config, "height" + std::to_string( i ), 1024 );
            if ( 0 == cfg.height )
            {
                QC_ERROR( "invalid height%u\n", i );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
        }
        else if ( "tensor" == typeStr )
        {
            cfg.type = DATA_READER_TYPE_TENSOR;

            cfg.tensorProps.tensorType =
                    Get( config, "tensor_type" + std::to_string( i ), QC_TENSOR_TYPE_FLOAT_32 );

            if ( QC_TENSOR_TYPE_MAX == cfg.tensorProps.tensorType )
            {
                QC_ERROR( "invalid tensor_type%u\n", i );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
            std::vector<uint32_t> dims;
            dims = Get( config, "dims" + std::to_string( i ), dims );
            if ( 0 == dims.size() )
            {
                QC_ERROR( "invalid dims%u\n", i );
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
            cfg.tensorProps.numDims = dims.size();
            for ( size_t i = 0; i < dims.size(); i++ )
            {
                cfg.tensorProps.dims[i] = dims[i];
            }
            cfg.tensorProps.allocatorType = QC_MEMORY_ALLOCATOR_DMA_HTP;
            cfg.tensorProps.cache = m_bufferCache;
        }
        else
        {
            QC_ERROR( "invalid data reader type %s\n", typeStr.c_str() );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }

        cfg.dataPath = Get( config, "data_path" + std::to_string( i ), "" );
        if ( "" == cfg.dataPath )
        {
            QC_INFO( "using dummy camera frame for %u\n", i );
        }

        m_configs.push_back( cfg );
    }

    m_fps = Get( config, "fps", 30.0f );
    if ( m_fps <= 0.0f )
    {
        QC_ERROR( "invalid fps = %f\n", m_fps );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_offset = Get( config, "offset", 0 );

    m_topicName = Get( config, "topic", "" );
    if ( "" == m_topicName )
    {
        QC_ERROR( "no topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_poolSize = Get( config, "pool_size", 4 );
    if ( 0 == m_poolSize )
    {
        QC_ERROR( "invalid pool_size = %d\n", m_poolSize );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

#ifdef QC_ENABLE_HS
    m_bEnableHsSleep = Get( config, "enable_hs_sleep", false );
#endif

    return ret;
}

QCStatus_e SampleDataReader::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = SampleIF::Init( name );
    if ( QC_STATUS_OK == ret )
    {
        TRACE_ON( DATA_READER );
        ret = ParseConfig( config );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_bufferPools.resize( m_numOfDataReaders );
        for ( uint32_t i = 0; ( i < m_numOfDataReaders ) && ( QC_STATUS_OK == ret ); i++ )
        {
            if ( DATA_READER_TYPE_IMAGE == m_configs[i].type )
            {
                ret = m_bufferPools[i].Init( name + std::to_string( i ), m_nodeId,
                                             LOGGER_LEVEL_INFO, m_poolSize, m_configs[i].width,
                                             m_configs[i].height, m_configs[i].format,
                                             QC_MEMORY_ALLOCATOR_DMA_CAMERA, m_bufferCache );
            }
            else
            {
                ret = m_bufferPools[i].Init( name + std::to_string( i ), m_nodeId,
                                             LOGGER_LEVEL_INFO, m_poolSize,
                                             m_configs[i].tensorProps );
            }
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_pub.Init( name, m_topicName );
    }

    return ret;
}

QCStatus_e SampleDataReader::Start()
{
    QCStatus_e ret = QC_STATUS_OK;
    m_stop = false;
    m_index = m_offset;
    m_frameId = 0;
#ifdef QC_ENABLE_HS
    // Start thread only if orchestrator is NOT enabled
    if ( !m_bOrchestratorEnabled )
    {
#endif
        m_thread = std::thread( &SampleDataReader::ThreadMain, this );
#ifdef QC_ENABLE_HS
    }
#endif

    return ret;
}

QCStatus_e SampleDataReader::LoadImage( std::shared_ptr<SharedBuffer_t> image, std::string path )
{
    QCStatus_e ret = QC_STATUS_OK;
    FILE *file = nullptr;
    size_t length = 0;
    QCBufferDescriptorBase_t &bufDesc = image->buffer;

    file = fopen( path.c_str(), "rb" );
    if ( nullptr == file )
    {
        QC_DEBUG( "Failed to open file %s", path.c_str() );
        ret = QC_STATUS_OUT_OF_BOUND;
    }

    if ( QC_STATUS_OK == ret )
    {
        fseek( file, 0, SEEK_END );
        length = (size_t) ftell( file );
        if ( bufDesc.size < length )
        {
            QC_ERROR( "Invalid image file %s", path.c_str() );
            ret = QC_STATUS_FAIL;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        fseek( file, 0, SEEK_SET );
        auto r = fread( bufDesc.pBuf, 1, length, file );
        if ( length != r )
        {
            QC_ERROR( "failed to read image file %s", path.c_str() );
            ret = QC_STATUS_FAIL;
        }
    }

    if ( nullptr != file )
    {
        fclose( file );
    }

    QC_DEBUG( "Loading Image %s %s", path.c_str(), ( QC_STATUS_OK == ret ) ? "OK" : "FAIL" );

    return ret;
}

QCStatus_e SampleDataReader::LoadTensor( std::shared_ptr<SharedBuffer_t> tensor, std::string path )
{
    QCStatus_e ret = QC_STATUS_OK;
    FILE *file = nullptr;
    size_t length = 0;
    uint32_t batchSize = 0;
    QCBufferDescriptorBase_t &bufDesc = tensor->buffer;
    uint32_t oneSize = 0;
    TensorDescriptor_t *pTensor = dynamic_cast<TensorDescriptor_t *>( &bufDesc );

    if ( nullptr == pTensor )
    {
        QC_ERROR( "Not a valid tensor descriptor" );
        ret = QC_STATUS_INVALID_BUF;
    }
    else
    {
        oneSize = s_qcTensorTypeToDataSize[pTensor->tensorType];
        for ( uint32_t i = 1; i < pTensor->numDims; i++ )
        {
            oneSize *= pTensor->dims[i];
        }

        file = fopen( path.c_str(), "rb" );
        if ( nullptr == file )
        {
            QC_DEBUG( "Failed to open file %s", path.c_str() );
            ret = QC_STATUS_OUT_OF_BOUND;
        }

        if ( QC_STATUS_OK == ret )
        {
            fseek( file, 0, SEEK_END );
            length = (size_t) ftell( file );
            batchSize = length / oneSize;
            if ( pTensor->size < length )
            {
                QC_ERROR( "Invalid Tensor file %s", path.c_str() );
                ret = QC_STATUS_FAIL;
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            fseek( file, 0, SEEK_SET );
            auto r = fread( pTensor->pBuf, 1, length, file );
            if ( length != r )
            {
                QC_ERROR( "failed to read PointCloud file %s", path.c_str() );
                ret = QC_STATUS_FAIL;
            }
            else
            {
                pTensor->dims[0] = batchSize;
            }
        }

        if ( nullptr != file )
        {
            fclose( file );
        }
    }

    QC_DEBUG( "Loading %u batch tensor %s %s", batchSize, path.c_str(),
              ( QC_STATUS_OK == ret ) ? "OK" : "FAIL" );

    return ret;
}

void SampleDataReader::Execute()
{
    QCStatus_e ret = QC_STATUS_OK;

    do
    {
        DataFrames_t frames;
        ret = QC_STATUS_OK;
        PROFILER_BEGIN();
        for ( uint32_t i = 0; ( i < m_numOfDataReaders ) && ( QC_STATUS_OK == ret ); i++ )
        {
            std::shared_ptr<SharedBuffer_t> buffer = m_bufferPools[i].Get();
            if ( nullptr != buffer )
            {
                if ( m_configs[i].dataPath != "" )
                {
                    std::string path;
                    if ( DATA_READER_TYPE_IMAGE == m_configs[i].type )
                    {
                        path = m_configs[i].dataPath + "/" + std::to_string( m_index ) +
                               s_qcFormatToStr[m_configs[i].format];
                        ret = LoadImage( buffer, path );
                    }
                    else
                    {
                        path = m_configs[i].dataPath + "/" + std::to_string( m_index ) + ".raw";
                        ret = LoadTensor( buffer, path );
                    }

                    if ( ( QC_STATUS_OK != ret ) && ( 0 == m_index ) )
                    {
                        QC_ERROR( "Invalid data path %u %s: no file %s", i,
                                  m_configs[i].dataPath.c_str(), path.c_str() );
                        m_stop = true;
                    }
                }
                else
                {
                    // skip loading, using dummy data
                }
                if ( QC_STATUS_OK == ret )
                {
                    DataFrame_t frame;
                    struct timespec ts;
                    clock_gettime( CLOCK_MONOTONIC, &ts );
                    frame.buffer = buffer;
                    frame.frameId = m_frameId;
                    frame.timestamp = ts.tv_sec * 1000000000 + ts.tv_nsec;
                    frames.Add( frame );
                }
                else
                {
                    ret = QC_STATUS_OUT_OF_BOUND;
                }
            }
            else
            {
                ret = QC_STATUS_NOMEM;
            }
        }

        if ( QC_STATUS_OK == ret )
        {
            PROFILER_END();
            TRACE_EVENT( m_frameId );
            m_pub.Publish( frames );
            m_index++;
            m_frameId++;
        }
        else if ( QC_STATUS_OUT_OF_BOUND == ret )
        {
            m_index = 0; /* retry load from beginning */
        }
        else
        {
            /* OK */
        }
    } while ( QC_STATUS_OUT_OF_BOUND == ret );
}

void SampleDataReader::ThreadMain()
{
    QCStatus_e ret = QC_STATUS_OK;
    uint32_t index = m_offset;
    uint64_t frameId = 0;
    while ( false == m_stop )
    {
        auto start = std::chrono::high_resolution_clock::now();
        Execute();
        auto end = std::chrono::high_resolution_clock::now();
        uint64_t elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>( end - start ).count();
        QC_DEBUG( "Loading frame %" PRIu64 " cost %" PRIu64 "ms", m_frameId, elapsedMs );
        if ( static_cast<uint64_t>( 1000 / m_fps ) > elapsedMs )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds(
                    static_cast<uint64_t>( 1000 / m_fps ) - elapsedMs ) );
        }
    }
}

QCStatus_e SampleDataReader::Stop()
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

    return ret;
}

QCStatus_e SampleDataReader::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;

    return ret;
}

REGISTER_SAMPLE( DataReader, SampleDataReader );

}   // namespace sample
}   // namespace QC
