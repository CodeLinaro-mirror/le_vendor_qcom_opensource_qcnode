// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/SimulationNode.hpp"
#include <unistd.h>

namespace QC
{
namespace Node
{

SimulationNodeConfig::SimulationNodeConfig( Logger &logger ) : NodeConfigBase( logger )
{
    // Initialize config with default values
    m_config.numOfEntries = 0;
    m_config.processingMode = SIMULATION_PROCESSING_SYNC;
    m_config.processingDelayMs = 0;
    m_config.numInputs = 1;
    m_config.numOutputs = 1;
    m_config.errorType = SIMULATION_ERROR_NONE;
    m_config.errorRate = 0;
    m_config.deadlockTimeoutSec = 0;
    m_config.timeoutSimulationSec = 0;
    m_config.forceState = false;
    m_config.forcedState = QC_OBJECT_STATE_READY;
    m_config.bDeRegisterAllBuffersWhenStop = false;
}

QCStatus_e SimulationNodeConfig::VerifyAndSet( const std::string config, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    status = NodeConfigBase::VerifyAndSet( config, errors );
    if ( status == QC_STATUS_OK )
    {
        DataTree dt;
        status = m_dataTree.Get( "static", dt );
        if ( status == QC_STATUS_OK )
        {
            status = ParseStaticConfig( dt, errors );
        }
        else
        {
            QC_ERROR( "SimulationNode only supports static config" );
        }
    }

    return status;
}

QCStatus_e SimulationNodeConfig::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    // Verify required fields
    std::string name = dt.Get<std::string>( "name", "" );
    if ( name.empty() )
    {
        errors += "the name is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    uint32_t id = dt.Get<uint32_t>( "id", UINT32_MAX );
    if ( id == UINT32_MAX )
    {
        errors += "the id is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    // Verify processing mode if present
    if ( dt.Exists( "processingMode" ) )
    {
        std::string processingModeStr = dt.Get<std::string>( "processingMode", "sync" );
        if ( processingModeStr != "sync" && processingModeStr != "async" )
        {
            errors += "the processingMode is invalid, must be 'sync' or 'async', ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    // Verify error type if present
    if ( dt.Exists( "errorType" ) )
    {
        std::string errorTypeStr = dt.Get<std::string>( "errorType", "none" );
        if ( errorTypeStr != "none" && errorTypeStr != "deadlock" && errorTypeStr != "timeout" &&
             errorTypeStr != "bad_arguments" )
        {
            errors += "the errorType is invalid, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    // Verify forced state if present
    if ( dt.Get<bool>( "forceState", false ) )
    {
        std::string forcedStateStr = dt.Get<std::string>( "forcedState", "" );
        if ( forcedStateStr != "initial" && forcedStateStr != "ready" &&
             forcedStateStr != "running" && forcedStateStr != "error" )
        {
            errors += "the forcedState is invalid, must be 'initial', 'ready', 'running', or "
                      "'error', ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    // Verify buffer IDs if present
    if ( dt.Exists( "bufferIds" ) )
    {
        std::vector<uint32_t> bufferIds = dt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        if ( bufferIds.empty() )
        {
            errors += "the bufferIds is invalid, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    // Verify global buffer ID map if present
    std::vector<DataTree> globalBufferIdMap;
    status2 = dt.Get( "globalBufferIdMap", globalBufferIdMap );
    if ( status2 == QC_STATUS_OUT_OF_BOUND )
    {
        // OK if not configured
    }
    else if ( status2 != QC_STATUS_OK )
    {
        errors += "the globalBufferIdMap is invalid, ";
        status = status2;
    }
    else
    {
        uint32_t idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            std::string name = gbm.Get<std::string>( "name", "" );
            uint32_t index = gbm.Get<uint32_t>( "id", UINT32_MAX );
            if ( name.empty() )
            {
                errors += "the globalIdMap " + std::to_string( idx ) + " name is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( index == UINT32_MAX )
            {
                errors += "the globalIdMap " + std::to_string( idx ) + " id is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            idx++;
        }
    }

    // Verify returnStatusForFunctions if present
    if ( dt.Exists( "returnStatusForFunctions" ) )
    {
        std::vector<DataTree> returnStatusFunctions;
        status2 = dt.Get( "returnStatusForFunctions", returnStatusFunctions );
        if ( status2 != QC_STATUS_OK )
        {
            errors += "the returnStatusForFunctions is invalid, ";
            status = status2;
        }
        else
        {
            uint32_t idx = 0;
            for ( DataTree &func : returnStatusFunctions )
            {
                std::string functionName = func.Get<std::string>( "function", "" );
                if ( functionName.empty() )
                {
                    errors += "the returnStatusForFunctions " + std::to_string( idx ) +
                              " function name is empty, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }

                // Status is optional, defaults to 0 (QC_STATUS_OK)
                idx++;
            }
        }
    }

    return status;
}

QCStatus_e SimulationNodeConfig::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    status = VerifyStaticConfig( dt, errors );
    if ( status == QC_STATUS_OK )
    {
        // Set node ID
        m_config.nodeId.name = dt.Get<std::string>( "name", "" );
        m_config.nodeId.id = dt.Get<uint32_t>( "id", UINT32_MAX );
        m_config.nodeId.type = QC_NODE_TYPE_CUSTOM_4;

        // Set name and id in the config structure
        m_config.name = m_config.nodeId.name;
        m_config.id = m_config.nodeId.id;

        // Parse processing mode
        std::string processingModeStr = dt.Get<std::string>( "processingMode", "sync" );
        if ( processingModeStr == "sync" )
        {
            m_config.processingMode = SIMULATION_PROCESSING_SYNC;
        }
        else if ( processingModeStr == "async" )
        {
            m_config.processingMode = SIMULATION_PROCESSING_ASYNC;
        }
        else
        {
            m_config.processingMode = SIMULATION_PROCESSING_SYNC;   // Default to sync
        }

        // Parse error type
        std::string errorTypeStr = dt.Get<std::string>( "errorType", "none" );
        if ( errorTypeStr == "none" )
        {
            m_config.errorType = SIMULATION_ERROR_NONE;
        }
        else if ( errorTypeStr == "deadlock" )
        {
            m_config.errorType = SIMULATION_ERROR_DEADLOCK;
        }
        else if ( errorTypeStr == "timeout" )
        {
            m_config.errorType = SIMULATION_ERROR_TIMEOUT;
        }
        else if ( errorTypeStr == "bad_arguments" )
        {
            m_config.errorType = SIMULATION_ERROR_BAD_ARGUMENTS;
        }
        else
        {
            m_config.errorType = SIMULATION_ERROR_NONE;   // Default to none
        }

        // Parse forced state
        m_config.forceState = dt.Get<bool>( "forceState", false );
        if ( m_config.forceState )
        {
            std::string forcedStateStr = dt.Get<std::string>( "forcedState", "ready" );
            if ( forcedStateStr == "initial" )
            {
                m_config.forcedState = QC_OBJECT_STATE_INITIAL;
            }
            else if ( forcedStateStr == "ready" )
            {
                m_config.forcedState = QC_OBJECT_STATE_READY;
            }
            else if ( forcedStateStr == "running" )
            {
                m_config.forcedState = QC_OBJECT_STATE_RUNNING;
            }
            else if ( forcedStateStr == "error" )
            {
                m_config.forcedState = QC_OBJECT_STATE_ERROR;
            }
            else
            {
                m_config.forcedState = QC_OBJECT_STATE_READY;
            }
        }

        m_config.processingDelayMs = dt.Get<uint32_t>( "processingDelayMs", 0 );
        m_config.numInputs = dt.Get<uint32_t>( "numInputs", 1 );
        m_config.numOutputs = dt.Get<uint32_t>( "numOutputs", 1 );
        m_config.errorRate = dt.Get<uint32_t>( "errorRate", 5 );

        m_config.deadlockTimeoutSec = dt.Get<uint32_t>( "deadlockTimeoutSec", 0 );
        m_config.timeoutSimulationSec = dt.Get<uint32_t>( "timeoutSimulationSec", 0 );

        m_config.returnStatusForFunctionMap.clear();
        if ( dt.Exists( "returnStatusForFunctions" ) )
        {
            std::vector<DataTree> returnStatusFunctions;
            status = dt.Get( "returnStatusForFunctions", returnStatusFunctions );

            if ( status == QC_STATUS_OK )
            {
                for ( size_t i = 0; i < returnStatusFunctions.size(); i++ )
                {
                    DataTree funcDt = returnStatusFunctions[i];

                    std::string functionName = funcDt.Get<std::string>( "function", "" );
                    int returnStatus = funcDt.Get<int>( "status", 0 );   // QC_STATUS_OK is 0

                    if ( !functionName.empty() )
                    {
                        m_config.returnStatusForFunctionMap[functionName] = returnStatus;
                        QC_INFO( "Setting return status %d for function %s", returnStatus,
                                 functionName.c_str() );
                    }
                }
            }
        }

        // Parse buffer IDs
        m_config.bufferIds = dt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

        // Parse global buffer ID map
        std::vector<DataTree> globalBufferIdMap;
        (void) dt.Get( "globalBufferIdMap", globalBufferIdMap );
        m_config.globalBufferIdMap.resize( globalBufferIdMap.size() );
        uint32_t idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            m_config.globalBufferIdMap[idx].name = gbm.Get<std::string>( "name", "" );
            m_config.globalBufferIdMap[idx].globalBufferId = gbm.Get<uint32_t>( "id", UINT32_MAX );
            idx++;
        }

        // Parse deRegisterAllBuffersWhenStop
        m_config.bDeRegisterAllBuffersWhenStop =
                dt.Get<bool>( "deRegisterAllBuffersWhenStop", false );

        // Set up global buffer ID map if not provided
        if ( m_config.globalBufferIdMap.empty() )
        {
            status = SetupGlobalBufferIdMap();
        }
    }
    else
    {
        QC_ERROR( "VerifyStaticConfig failed!" );
    }

    return status;
}

QCStatus_e SimulationNodeConfig::SetupGlobalBufferIdMap()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( m_config.globalBufferIdMap.size() > 0 )
    {
        if ( m_config.globalBufferIdMap.size() < ( m_config.numInputs + m_config.numOutputs ) )
        {
            QC_ERROR( "Global buffer map size is not sufficient: expect at least %" PRIu32,
                      m_config.numInputs + m_config.numOutputs );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }
    else
    {
        m_config.globalBufferIdMap.resize( m_config.numInputs + m_config.numOutputs );
        uint32_t globalBufferId = 0;

        for ( uint32_t i = 0; i < m_config.numInputs; i++ )
        {
            m_config.globalBufferIdMap[globalBufferId].name = "input" + std::to_string( i );
            m_config.globalBufferIdMap[globalBufferId].globalBufferId = globalBufferId;
            globalBufferId++;
        }

        for ( uint32_t i = 0; i < m_config.numOutputs; i++ )
        {
            m_config.globalBufferIdMap[globalBufferId].name = "output" + std::to_string( i );
            m_config.globalBufferIdMap[globalBufferId].globalBufferId = globalBufferId;
            globalBufferId++;
        }
    }

    return status;
}

const std::string &SimulationNodeConfig::GetOptions()
{
    if ( m_options.empty() )
    {
        DataTree dt;
        dt.Set<uint32_t>( "version", QCNODE_SIMULATION_NODE_VERSION );
        dt.Set<std::string>( "description", "SimulationNode configuration options" );

        // Processing modes
        dt.Set<std::string>( "processingMode_sync", "Synchronous processing mode" );
        dt.Set<std::string>( "processingMode_async", "Asynchronous processing mode" );

        // Error types
        dt.Set<std::string>( "errorType_none", "No errors" );
        dt.Set<std::string>( "errorType_deadlock", "Simulate deadlock" );
        dt.Set<std::string>( "errorType_timeout", "Simulate timeout" );
        dt.Set<std::string>( "errorType_bad_arguments", "Return bad arguments error" );

        // Forced states
        dt.Set<std::string>( "forcedState_initial", "Force initial state" );
        dt.Set<std::string>( "forcedState_ready", "Force ready state" );
        dt.Set<std::string>( "forcedState_running", "Force running state" );
        dt.Set<std::string>( "forcedState_error", "Force error state" );

        // Return status functions
        dt.Set<std::string>( "returnStatusForFunctions",
                             "Configure return status for specific functions" );
        dt.Set<std::string>( "returnStatusForFunctions_Initialize",
                             "Configure return status for Initialize" );
        dt.Set<std::string>( "returnStatusForFunctions_DeInitialize",
                             "Configure return status for DeInitialize" );
        dt.Set<std::string>( "returnStatusForFunctions_Start",
                             "Configure return status for Start" );
        dt.Set<std::string>( "returnStatusForFunctions_Stop", "Configure return status for Stop" );
        dt.Set<std::string>( "returnStatusForFunctions_ProcessFrameDescriptor",
                             "Configure return status for ProcessFrameDescriptor" );
        dt.Set<std::string>( "returnStatusForFunctions_GetState",
                             "Configure return status for GetState" );

        // Add timeout configuration options
        dt.Set<std::string>( "deadlockTimeoutSec", "Timeout in seconds for deadlock simulation" );
        dt.Set<std::string>( "timeoutSimulationSec", "Timeout in seconds for timeout simulation" );

        m_options = dt.Dump();
    }
    return m_options;
}

const QCNodeConfigBase_t &SimulationNodeConfig::Get()
{
    return m_config;
}

}   // namespace Node
}   // namespace QC