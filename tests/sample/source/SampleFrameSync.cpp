// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "QC/sample/SampleFrameSync.hpp"
#include "TimestampSync.hpp"

namespace QC
{
namespace sample
{

SampleFrameSync::SampleFrameSync() {}
SampleFrameSync::~SampleFrameSync() {}

#ifdef QC_ENABLE_HS
std::function<void( const std::uint32_t *, std::size_t )> SampleFrameSync::GetRunnableCallback()
{
    m_bOrchestratorEnabled = true;
    return std::bind( &SampleFrameSync::RunnableCallback, this, std::placeholders::_1,
                      std::placeholders::_2 );
}

void SampleFrameSync::RunnableCallback( const std::uint32_t *rids, std::size_t count )
{
    Execute();
}
#endif

QCStatus_e SampleFrameSync::WaitReady()
{
    QCStatus_e ret = QC_STATUS_OK;

    for ( uint32_t i = 0; ( i < m_number ) && ( QC_STATUS_OK == ret ); i++ )
    {
        ret = m_subs[i].WaitUntilFrame( 1000 );
        if ( QC_STATUS_OK != ret )
        {
            QC_ERROR( "input %u not ready after 1000 ms", i );
        }
    }

    return ret;
}

QCStatus_e SampleFrameSync::ParseConfig( SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    m_number = Get( config, "number", 1 );
    if ( m_number < 1 )
    {
        QC_ERROR( "invalid number = %u\n", m_number );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_windowMs = Get( config, "window", 100 );

    for ( uint32_t i = 0; i < m_number; i++ )
    {
        std::string topicName = Get( config, "input_topic" + std::to_string( i ), "" );
        if ( "" == topicName )
        {
            QC_ERROR( "no input_topic%u\n", i );
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        m_inputTopicNames.push_back( topicName );
    }

    std::vector<uint32_t> perms;
    m_perms = Get( config, "perms", perms );

    m_outputTopicName = Get( config, "output_topic", "" );
    if ( "" == m_outputTopicName )
    {
        QC_ERROR( "no output topic\n" );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    auto syncModeStr = Get( config, "mode", "window" );

    if ( "window" == syncModeStr )
    {
        m_syncMode = FRAME_SYNC_MODE_WINDOW;
    }
    else if ( "buffer_timestamp" == syncModeStr )
    {
        m_syncMode = FRAME_SYNC_MODE_BUFFER_TIMESTAMP;
    }
    else
    {
        QC_ERROR( "invalid mode %s\n", syncModeStr.c_str() );
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    m_timestampThresholdMs = Get( config, "timestamp_threshold_ms", (uint32_t) 10 );
    m_queueDepth = Get( config, "queue_depth", (uint32_t) 1 );

    return ret;
}

QCStatus_e SampleFrameSync::Init( std::string name, SampleConfig_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    ret = SampleIF::Init( name );
    if ( QC_STATUS_OK == ret )
    {
        ret = ParseConfig( config );
    }

    if ( QC_STATUS_OK == ret )
    {
        m_subs.resize( m_number );
        uint32_t queueDepth = m_queueDepth;
        for ( uint32_t i = 0; ( i < m_number ) && ( QC_STATUS_OK == ret ); i++ )
        {
            ret = m_subs[i].Init( name + "_" + std::to_string( i ), m_inputTopicNames[i],
                                  queueDepth );
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = m_pub.Init( name, m_outputTopicName );
    }

    return ret;
}

QCStatus_e SampleFrameSync::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    m_stop = false;
#ifdef QC_ENABLE_HS
    if ( !m_bOrchestratorEnabled )
    {
#endif
        if ( FRAME_SYNC_MODE_WINDOW == m_syncMode )
        {
            m_thread = std::thread( &SampleFrameSync::threadWindowMain, this );
        }
        else if ( FRAME_SYNC_MODE_BUFFER_TIMESTAMP == m_syncMode )
        {
            m_thread = std::thread( &SampleFrameSync::threadBufferTimestampMain, this );
        }
#ifdef QC_ENABLE_HS
    }
#endif

    return ret;
}

void SampleFrameSync::Execute()
{
    if ( FRAME_SYNC_MODE_BUFFER_TIMESTAMP == m_syncMode )
    {
        ExecuteBufferTimestamp();
    }
    else
    {
        ExecuteWindowSync();
    }
}

void SampleFrameSync::ExecuteWindowSync()
{
    QCStatus_e ret;
    uint64_t timeoutMs = (uint64_t) m_windowMs;
    std::vector<DataFrames_t> framesList;
    DataFrames_t frames;
    uint64_t frameId;
    ret = m_subs[0].Receive( frames, timeoutMs );
    if ( QC_STATUS_OK == ret )
    {
        framesList.push_back( frames );
        frameId = frames.FrameId( 0 );
        auto begin = std::chrono::high_resolution_clock::now();
        PROFILER_BEGIN();
        TRACE_BEGIN( frameId );
        QC_DEBUG( "[0]receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", frames.FrameId( 0 ),
                  frames.Timestamp( 0 ) );
        for ( uint32_t i = 1; i < m_number; i++ )
        {
            auto now = std::chrono::high_resolution_clock::now();
            uint64_t elapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>( now - begin ).count();
            if ( (uint64_t) m_windowMs > elapsedMs )
            {
                timeoutMs = (uint64_t) m_windowMs - elapsedMs;
                ret = m_subs[i].Receive( frames, timeoutMs );
            }
            else
            {
                ret = QC_STATUS_TIMEOUT;
            }

            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "input %u frame not ready in %u ms", i, m_windowMs );
                break;
            }
            else
            {
                framesList.push_back( frames );
                QC_DEBUG( "[%u]receive frameId %" PRIu64 ", timestamp %" PRIu64 "\n", i,
                          frames.FrameId( 0 ), frames.Timestamp( 0 ) );
            }
        }
        if ( framesList.size() == (size_t) m_number )
        {
            PublishFrames( framesList, frameId );
            PROFILER_END();
            TRACE_END( frameId );
        }
    }
#ifdef QC_ENABLE_HS
    else if ( m_bOrchestratorEnabled )
    {
        QC_ERROR( "FrameSync receive failed : %d", ret );
    }
#endif
}

void SampleFrameSync::ExecuteBufferTimestamp()
{
    /*
     * BUFFER_TIMESTAMP SYNC — OVERVIEW
     * =================================
     *
     * Each camera's DataSubscriber holds a ring of the last `queue_depth` frames
     * (config key "queue_depth", default 1).  Peek() returns a non-consuming
     * snapshot ordered oldest→newest.
     *
     * Goal: pick one frame per camera so that max_ts − min_ts is minimised.
     * Implemented via FindMinSpreadIndices() — see TimestampSync.hpp for the
     * full algorithm description, complexity analysis, and worked example.
     *
     * ── queue_depth=1 (degenerate case) ────────────────────────────────────────
     *
     *   Each camera has exactly one buffered frame.  The window holds a single
     *   candidate per camera; the max-camera pointer is already at 0, so the
     *   loop exits immediately after the initial evaluation.
     *
     * ── queue_depth=2+ (general case) ──────────────────────────────────────────
     *
     * ── Example: 3 cameras, queue_depth=3, 30ms frame gap, timestamps in µs ───
     *
     *   Cam 0 buffer:  idx[0]=100000  idx[1]=130000  idx[2]=160000  (oldest→newest, 30ms gap)
     *   Cam 1 buffer:  idx[0]=103000  idx[1]=133000  idx[2]=163000  (3ms late,  30ms gap)
     *   Cam 2 buffer:  idx[0]=098000  idx[1]=128000                 (2ms early, only 2 arrived)
     *
     *   Step 0  ptrs=[2,2,1]  spread=35ms
     *   Step 1  max=cam1→ptr1=1  spread=32ms  ✓
     *   Step 2  max=cam0→ptr0=1  spread= 5ms  ✓  ← done (≤ threshold)
     *
     *   Result: ptrs=[1,1,1], spread=5ms → freshest frame set with minimum spread
     */

    /* --- Step 1: Peek all subscribers — non-consuming snapshot ----------- */
    std::vector<std::vector<DataFrames_t>> camFrames( m_number );
    bool allReady = true;

    for ( uint32_t i = 0; ( i < m_number ) && allReady; i++ )
    {
        m_subs[i].Peek( camFrames[i] );
        if ( camFrames[i].empty() )
        {
            QC_ERROR( "cam %u peek returned no frames", i );
            allReady = false;
        }
    }

    if ( true == allReady )
    {
        uint64_t frameId = camFrames[0].back().FrameId( 0 );
        PROFILER_BEGIN();
        TRACE_BEGIN( frameId );

        uint64_t threshold = (uint64_t) m_timestampThresholdMs * 1000ULL; /* ms → µs */

        /* --- Step 2: Sliding-window search for minimum timestamp spread ---- */
        std::vector<std::vector<uint64_t>> timestamps( m_number );
        for ( uint32_t i = 0; i < m_number; i++ )
        {
            timestamps[i].reserve( camFrames[i].size() );
            for ( auto &f : camFrames[i] )
            {
                timestamps[i].push_back( f.Timestamp( 0 ) );
            }
        }

        std::vector<size_t> bestSel;
        uint64_t bestSpread = FindMinSpreadIndices( timestamps, threshold, bestSel );

        /* --- Step 3: Assemble and publish the best-matched frame set ----- */
        QC_DEBUG( "buffer_timestamp: spread=%" PRIu64 " us (threshold=%" PRIu32 " ms)\n",
                  bestSpread, m_timestampThresholdMs );

        std::vector<DataFrames_t> selectedFrames;
        selectedFrames.reserve( m_number );
        for ( uint32_t i = 0; i < m_number; i++ )
        {
            selectedFrames.push_back( camFrames[i][bestSel[i]] );
        }
        PublishFrames( selectedFrames, frameId );
        PROFILER_END();
        TRACE_END( frameId );
    }
}

void SampleFrameSync::PublishFrames( std::vector<DataFrames_t> &selectedFrames, uint64_t frameId )
{
    DataFrames_t outFrames;
    for ( auto &frames : selectedFrames )
    {
        for ( auto &frame : frames.frames )
        {
            outFrames.Add( frame );
        }
    }

    QCStatus_e ret = QC_STATUS_OK;
    if ( ( m_perms.size() > 0 ) && ( m_perms.size() <= outFrames.frames.size() ) )
    {
        DataFrames_t newFrames;
        for ( auto idx : m_perms )
        {
            if ( idx < outFrames.frames.size() )
            {
                newFrames.Add( outFrames.frames[idx] );
            }
            else
            {
                QC_ERROR( "perms index %" PRIu32 " out of range %" PRIu64, idx,
                          outFrames.frames.size() );
                ret = QC_STATUS_OUT_OF_BOUND;
                break;
            }
        }
        if ( QC_STATUS_OK == ret )
        {
            m_pub.Publish( newFrames );
        }
    }
    else
    {
        m_pub.Publish( outFrames );
    }
}

void SampleFrameSync::threadBufferTimestampMain()
{
    WaitReady();
    uint64_t windowNs = m_windowMs * 1000000;


    while ( false == m_stop )
    {
        auto cycleStart = std::chrono::steady_clock::now();
        ExecuteBufferTimestamp();
        uint64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - cycleStart )
                                   .count();
        if ( elapsed < windowNs )
        {
            std::this_thread::sleep_for( std::chrono::nanoseconds( windowNs - elapsed ) );
        }
    }
}

void SampleFrameSync::threadWindowMain()
{
    while ( false == m_stop )
    {
        ExecuteWindowSync();
    }
}


QCStatus_e SampleFrameSync::Stop()
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

QCStatus_e SampleFrameSync::Deinit()
{
    QCStatus_e ret = QC_STATUS_OK;
    return ret;
}

REGISTER_SAMPLE( FrameSync, SampleFrameSync );

}   // namespace sample
}   // namespace QC
