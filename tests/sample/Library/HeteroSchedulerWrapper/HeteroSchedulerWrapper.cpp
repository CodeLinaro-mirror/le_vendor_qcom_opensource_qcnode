// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file HeteroSchedulerWrapper.cpp
 * @brief Implementation of the HeteroSchedulerWrapper utility class.
 */

#include "HeteroSchedulerWrapper.hpp"
#include "QC/sample/SampleIF.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace QC
{
namespace sample
{

HeteroSchedulerWrapper::HeteroSchedulerWrapper()
    : m_clientName( "" ),
      m_bInitialized( false ),
      m_bRunning( false )
{
    QC_LOGGER_INIT( "HeteroSchedulerWrapper", LOGGER_LEVEL_INFO );
}

HeteroSchedulerWrapper &HeteroSchedulerWrapper::getInstance()
{
    static HeteroSchedulerWrapper instance;
    return instance;
}

HeteroSchedulerWrapper::~HeteroSchedulerWrapper()
{
    Cleanup();
}

QCStatus_e HeteroSchedulerWrapper::Initialize( const std::string &clientName )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( m_bInitialized )
    {
        QC_ERROR( "[HeteroSchedulerClient] Already initialized" );
        status = QC_STATUS_FAIL;
    }
    else
    {
        // Store the client name
        m_clientName = clientName;

        // Step 1: Initialize orchestrator client runtime
        cf::CF_ORCH_ERROR orchInitResult = cf::OrchInit( m_clientName.c_str(), nullptr );
        if ( orchInitResult != cf::CF_ORCH_ERROR::SUCCESS )
        {
            QC_ERROR( "[HeteroSchedulerClient] OrchInit failed for client: %s, error: %d",
                      m_clientName.c_str(), static_cast<int>( orchInitResult ) );
            status = QC_STATUS_FAIL;
        }
        else
        {
            // Step 2: Obtain the names of node vertices
            cf::CF_ORCH_ERROR orchVertListResult = cf::OrchVertList( m_vertNames );
            if ( orchVertListResult != cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "[HeteroSchedulerClient] OrchVertList failed, error: %d",
                          static_cast<int>( orchVertListResult ) );
                cf::CF_ORCH_ERROR orchDeinitResult = cf::OrchDeinit();
                if ( orchDeinitResult != cf::CF_ORCH_ERROR::SUCCESS )
                {
                    QC_ERROR( "[HeteroSchedulerClient] OrchDeinit failed during cleanup, error: %d",
                              static_cast<int>( orchDeinitResult ) );
                }
                status = QC_STATUS_FAIL;
            }
            else
            {
                m_bInitialized = true;
                QC_DEBUG( "[HeteroSchedulerClient] Initialized successfully for client: %s",
                          m_clientName.c_str() );
            }
        }
    }

    return status;
}


QCStatus_e HeteroSchedulerWrapper::RegisterVertex( const std::vector<SampleIF *> &samples )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( !m_bInitialized )
    {
        QC_ERROR( "[HeteroSchedulerClient] Cannot register vertex - client not initialized" );
        status = QC_STATUS_FAIL;
    }
    else if ( m_bRunning )
    {
        QC_ERROR( "[HeteroSchedulerClient] Cannot register vertex - client already running" );
        status = QC_STATUS_FAIL;
    }
    else
    {
        for ( auto const &name : m_vertNames )
        {
            // Resolve vertex ID from task name
            std::uint32_t vid = 0;
            cf::CF_ORCH_ERROR orchVertIdResult = cf::OrchVertIdFromString( name.c_str(), &vid );
            if ( orchVertIdResult != cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "[HeteroSchedulerClient] OrchVertIdFromString failed for task %s, "
                          "error: %d",
                          name.c_str(), static_cast<int>( orchVertIdResult ) );
                continue;
            }

            // Find the sample whose name matches the task hash
            SampleIF *matchingSample = nullptr;
            for ( auto sample : samples )
            {
                if ( sample && std::string( sample->GetName() ) == name )
                {
                    matchingSample = sample;
                    break;
                }
            }

            if ( matchingSample )
            {
                VertexCallback cb = matchingSample->GetRunnableCallback();

                // Check if callback is valid (sample supports orchestrator mode)
                if ( cb )
                {
                    std::function<int( const std::uint32_t *, std::size_t )> run_cb;

                    run_cb = [cb]( const std::uint32_t *rids, std::size_t count ) {
                        cb( rids, count );
                        return 0;
                    };

                    // Register the callback with the orchestrator
                    cf::CF_ORCH_ERROR orchVertRegResult = cf::OrchVertRegister( 0, vid, run_cb );
                    if ( orchVertRegResult != cf::CF_ORCH_ERROR::SUCCESS )
                    {
                        QC_ERROR( "[HeteroSchedulerClient] OrchVertRegister failed for VID %u "
                                  "(task %s), error: %d",
                                  vid, name.c_str(), static_cast<int>( orchVertRegResult ) );
                        status = QC_STATUS_FAIL;
                        break;
                    }

                    QC_DEBUG( "[HeteroSchedulerClient] Registered vertex: %s (VID: %u)",
                              name.c_str(), vid );
                }
                else
                {
                    QC_DEBUG(
                            "[HeteroSchedulerClient] Sample %s does not support orchestrator mode",
                            matchingSample->GetName() );
                }
            }
            else
            {
                QC_DEBUG( "[HeteroSchedulerClient] Warning: No sample found with name matching "
                          "task hash '%s'",
                          name.c_str() );
            }
        }

        // Register the client with the orchestrator
        if ( status == QC_STATUS_OK )
        {
            cf::CF_ORCH_ERROR orchClientRegResult = cf::OrchClientRegister( m_clientName.c_str() );
            if ( orchClientRegResult != cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "[HeteroSchedulerClient] OrchClientRegister failed for client: %s, "
                          "error: %d",
                          m_clientName.c_str(), static_cast<int>( orchClientRegResult ) );
                status = QC_STATUS_FAIL;
            }
        }
    }

    return status;
}

QCStatus_e HeteroSchedulerWrapper::Start()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( !m_bInitialized )
    {
        QC_ERROR( "[HeteroSchedulerClient] Cannot start - client not initialized" );
        status = QC_STATUS_FAIL;
    }
    else if ( m_bRunning )
    {
        QC_ERROR( "[HeteroSchedulerClient] Client already running" );
        status = QC_STATUS_FAIL;
    }
    else
    {
        // Check for optional start delay controlled by QC_HS_START_DELAY (in ms)
        const char *delayEnv = std::getenv( "QC_HS_START_DELAY" );
        if ( delayEnv != nullptr )
        {
            int delayMs = std::atoi( delayEnv );
            if ( delayMs > 0 )
            {
                QC_DEBUG( "[HeteroSchedulerClient] Delaying OrchClientStart by %d ms "
                          "(QC_HS_START_DELAY)",
                          delayMs );
                std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );
            }
        }

        // Start the client execution
        cf::CF_ORCH_ERROR orchStartResult = cf::OrchClientStart( m_clientName.c_str() );
        if ( orchStartResult != cf::CF_ORCH_ERROR::SUCCESS )
        {
            QC_ERROR( "[HeteroSchedulerClient] OrchClientStart failed for client: %s, error: %d",
                      m_clientName.c_str(), static_cast<int>( orchStartResult ) );
            cf::CF_ORCH_ERROR orchStopResult = cf::OrchClientStop( m_clientName.c_str() );
            if ( orchStopResult != cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "[HeteroSchedulerClient] OrchClientStop failed during cleanup for "
                          "client: %s, error: %d",
                          m_clientName.c_str(), static_cast<int>( orchStopResult ) );
            }
            status = QC_STATUS_FAIL;
        }
        else
        {
            m_bRunning = true;
            QC_DEBUG( "[HeteroSchedulerClient] Started successfully for client: %s",
                      m_clientName.c_str() );
        }
    }

    return status;
}

QCStatus_e HeteroSchedulerWrapper::Stop()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( !m_bRunning )
    {
        // Not an error - already stopped
    }
    else
    {
        // Stop the client execution
        cf::CF_ORCH_ERROR result = cf::OrchClientStop( m_clientName.c_str() );
        m_bRunning = false;

        if ( result != cf::CF_ORCH_ERROR::SUCCESS )
        {
            QC_ERROR( "[HeteroSchedulerClient] OrchClientStop failed for client: %s, error: %d",
                      m_clientName.c_str(), static_cast<int>( result ) );
            status = QC_STATUS_FAIL;
        }
        else
        {
            QC_DEBUG( "[HeteroSchedulerClient] Stopped successfully for client: %s",
                      m_clientName.c_str() );
        }
    }

    return status;
}

QCStatus_e HeteroSchedulerWrapper::Deinit()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( !m_bInitialized )
    {
        QC_ERROR( "[HeteroSchedulerClient] Cannot deinit - client not initialized" );
        status = QC_STATUS_FAIL;
    }
    else
    {
        // Stop if still running
        if ( m_bRunning )
        {
            QCStatus_e stopResult = Stop();
            if ( stopResult != QC_STATUS_OK )
            {
                QC_ERROR( "[HeteroSchedulerClient] Warning: stop() failed during deinit" );
            }
        }

        // Deinitialize orchestrator
        cf::CF_ORCH_ERROR result = cf::OrchDeinit();
        if ( result != cf::CF_ORCH_ERROR::SUCCESS )
        {
            QC_ERROR( "[HeteroSchedulerClient] OrchDeinit failed for client: %s, error: %d",
                      m_clientName.c_str(), static_cast<int>( result ) );
            m_bInitialized = false;
            status = QC_STATUS_FAIL;
        }
        else
        {
            m_bInitialized = false;
            QC_DEBUG( "[HeteroSchedulerClient] Deinitialized successfully for client: %s",
                      m_clientName.c_str() );
        }
    }

    return status;
}

void HeteroSchedulerWrapper::Cleanup()
{
    // Stop if running
    if ( m_bRunning )
    {
        Stop();
    }

    // Note: OrchDeinit is now called via the public deinit() API
    // The destructor will not call OrchDeinit to allow explicit control
    if ( m_bInitialized )
    {
        m_bInitialized = false;
        QC_DEBUG( "[HeteroSchedulerClient] Cleaned up for client: %s (OrchDeinit not called - use "
                  "deinit() explicitly)",
                  m_clientName.c_str() );
    }
}

}   // namespace sample
}   // namespace QC
