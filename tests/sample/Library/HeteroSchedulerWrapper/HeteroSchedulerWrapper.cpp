// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file HeteroSchedulerWrapper.cpp
 * @brief Implementation of the HeteroSchedulerWrapper utility class.
 */

#include "HeteroSchedulerWrapper.hpp"
#include "QC/sample/SampleIF.hpp"
#include <cstdio>

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
        if ( cf::OrchInit( m_clientName.c_str(), nullptr ) != cf::CF_ORCH_ERROR::SUCCESS )
        {
            QC_ERROR( "[HeteroSchedulerClient] OrchInit failed for client: %s",
                      m_clientName.c_str() );
            status = QC_STATUS_FAIL;
        }
        else if ( cf::OrchVertList( m_vertNames ) != cf::CF_ORCH_ERROR::SUCCESS )
        {
            // Step 2: Obtain the names of node vertices
            QC_ERROR( "[HeteroSchedulerClient] OrchVertList failed" );
            (void)cf::OrchDeinit();
            status = QC_STATUS_FAIL;
        }
        else
        {
            m_bInitialized = true;
            QC_DEBUG( "[HeteroSchedulerClient] Initialized successfully for client: %s",
                      m_clientName.c_str() );
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
            if ( cf::OrchVertIdFromString( name.c_str(), &vid ) !=
                 cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "OrchVertIdFromString failed for task %s", name.c_str() );
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

                    // 6.3 Register the callback with the orchestrator
                    if ( cf::OrchVertRegister( 0, vid, run_cb ) != cf::CF_ORCH_ERROR::SUCCESS )
                    {
                        QC_ERROR( "OrchVertRegister failed for VID %u (task %s)", vid,
                                  name.c_str() );
                        status = QC_STATUS_FAIL;
                        break;
                    }

                    QC_DEBUG( "[HeteroSchedulerClient] Registered vertex: %s (VID: %u)",
                              name.c_str(), vid );
                }
                else
                {
                    QC_DEBUG( "[HeteroSchedulerClient] Sample %s does not support orchestrator mode",
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
            if ( cf::OrchClientRegister( m_clientName.c_str() ) !=
                 cf::CF_ORCH_ERROR::SUCCESS )
            {
                QC_ERROR( "[HeteroSchedulerClient] OrchClientRegister failed for client: %s",
                          m_clientName.c_str() );
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
    else if ( cf::OrchClientStart( m_clientName.c_str() ) != cf::CF_ORCH_ERROR::SUCCESS )
    {
        // Start the client execution
        QC_ERROR( "[HeteroSchedulerClient] OrchClientStart failed for client: %s",
                  m_clientName.c_str() );
        cf::OrchClientStop( m_clientName.c_str() );
        status = QC_STATUS_FAIL;
    }
    else
    {
        m_bRunning = true;
        QC_DEBUG( "[HeteroSchedulerClient] Started successfully for client: %s",
                  m_clientName.c_str() );
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
            QC_ERROR( "[HeteroSchedulerClient] OrchClientStop failed for client: %s",
                      m_clientName.c_str() );
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
            QC_ERROR( "[HeteroSchedulerClient] OrchDeinit failed for client: %s",
                      m_clientName.c_str() );
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
