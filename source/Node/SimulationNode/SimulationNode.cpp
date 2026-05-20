// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/SimulationNode.hpp"
#include <chrono>
#include <cstring>
#include <thread>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_RESERVED, SimulationNode )

SimulationNode::SimulationNode()
    : m_configIfs( m_logger ),
      m_monitorIfs( m_logger ),
      m_state( QC_OBJECT_STATE_INITIAL ),
      m_frameCount( 0 )
{}

SimulationNode::~SimulationNode()
{
    // No dynamic memory to clean up
}

/**
 * Initialize the node with the provided configuration
 * @param config Node initialization configuration
 * @return Status of the initialization operation
 */
QCStatus_e SimulationNode::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    bool bNodeBaseInitDone = false;

    // Verify and set configuration
    status = m_configIfs.VerifyAndSet( config.config, errors );

    if ( status != QC_STATUS_OK )
    {
        QC_ERROR( "Config error: %s", errors.c_str() );
        return status;
    }

    // Initialize the base node
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );
    status = NodeBase::Init( cfg.nodeId );

    if ( status != QC_STATUS_OK )
    {
        QC_ERROR( "Failed to initialize node base with status: %d", status );
        return status;
    }

    bNodeBaseInitDone = true;

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "Initialize" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return static_cast<QCStatus_e>( it->second );
    }

    // Store callback
    m_callback = config.callback;

    // Store references to the provided buffers
    for ( uint32_t bufferId : cfg.bufferIds )
    {
        if ( bufferId < config.buffers.size() )
        {
            m_registeredBuffers.push_back( config.buffers[bufferId] );
        }
        else
        {
            QC_ERROR( "Buffer index %u out of range (max: %zu)", bufferId,
                      config.buffers.size() - 1 );
            status = QC_STATUS_BAD_ARGUMENTS;
            break;
        }
    }

    // Set state
    if ( status == QC_STATUS_OK && !cfg.forceState )
    {
        m_state = QC_OBJECT_STATE_READY;
    }
    else if ( cfg.forceState )
    {
        m_state = cfg.forcedState;
        QC_INFO( "SimulationNode forcing state to %d", m_state );
    }

    if ( status != QC_STATUS_OK )
    {
        // Clean up on error
        if ( bNodeBaseInitDone )
        {
            QCStatus_e cleanupStatus = NodeBase::DeInitialize();
            if ( cleanupStatus != QC_STATUS_OK )
            {
                QC_ERROR( "Failed to clean up node base with status: %d", cleanupStatus );
            }
        }
    }

    return status;
}

/**
 * Start the node processing
 * @return Status of the start operation
 */
QCStatus_e SimulationNode::Start()
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "Start" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return static_cast<QCStatus_e>( it->second );
    }

    QCStatus_e status = QC_STATUS_OK;

    if ( cfg.forceState )
    {
        m_state = cfg.forcedState;
        QC_INFO( "SimulationNode forcing state to %d", m_state );
    }

    if ( m_state == QC_OBJECT_STATE_READY )
    {
        if ( !cfg.forceState )
        {
            m_state = QC_OBJECT_STATE_RUNNING;
        }
    }
    else
    {
        QC_ERROR( "SimulationNode start failed due to wrong state: %d", m_state );
        status = QC_STATUS_BAD_STATE;
    }

    return status;
}

/**
 * Process a frame descriptor
 * @param frameDesc Frame descriptor to process
 * @return Status of the processing operation
 */
QCStatus_e SimulationNode::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "ProcessFrameDescriptor" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return static_cast<QCStatus_e>( it->second );
    }

    if ( cfg.forceState )
    {
        m_state = cfg.forcedState;
        QC_INFO( "SimulationNode forcing state to %d", m_state );
    }

    QCStatus_e status = QC_STATUS_OK;

    // Increment frame counter
    m_frameCount++;

    if ( m_state != QC_OBJECT_STATE_RUNNING )
    {
        QC_ERROR( "SimulationNode not in running state! Current state: %d", m_state );
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        // Check if we should simulate an error based on error type and rate
        if ( cfg.errorType != SIMULATION_ERROR_NONE &&
             ( cfg.errorRate > 0 && m_frameCount % cfg.errorRate == 0 ) )
        {
            status = SimulateError( cfg.errorType );

            if ( status != QC_STATUS_OK )
            {
                QC_ERROR( "Error simulation returned status: %d", status );
                return status;
            }
        }

        status = ProcessBuffers( frameDesc );
        if ( status != QC_STATUS_OK )
        {
            QC_ERROR( "Buffer processing failed with status: %d", status );
            return status;
        }

        // Simulate processing delay
        if ( cfg.processingDelayMs > 0 )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( cfg.processingDelayMs ) );
        }

        // Call the callback to notify completion only in async mode
        if ( cfg.processingMode == SIMULATION_PROCESSING_ASYNC && m_callback )
        {
            QCNodeEventInfo_t eventInfo( frameDesc, m_nodeId, status, m_state );
            m_callback( eventInfo );
        }
    }

    return status;
}

/**
 * Stop the node processing
 * @return Status of the stop operation
 */
QCStatus_e SimulationNode::Stop()
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "Stop" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return static_cast<QCStatus_e>( it->second );
    }

    if ( cfg.forceState )
    {
        m_state = cfg.forcedState;
        QC_INFO( "SimulationNode forcing state to %d", m_state );
    }

    QCStatus_e status = QC_STATUS_OK;

    if ( m_state == QC_OBJECT_STATE_RUNNING )
    {
        if ( status == QC_STATUS_OK && !cfg.forceState )
        {
            m_state = QC_OBJECT_STATE_READY;
        }

        // If configured to deregister buffers on stop
        if ( cfg.bDeRegisterAllBuffersWhenStop )
        {
            m_registeredBuffers.clear();
        }
    }
    else
    {
        QC_ERROR( "SimulationNode stop failed due to wrong state: %d", m_state );
        status = QC_STATUS_BAD_STATE;
    }

    return status;
}

/**
 * De-initialize the node and release resources
 * @return Status of the de-initialization operation
 */
QCStatus_e SimulationNode::DeInitialize()
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "DeInitialize" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return static_cast<QCStatus_e>( it->second );
    }

    if ( cfg.forceState )
    {
        m_state = cfg.forcedState;
        QC_INFO( "SimulationNode forcing state to %d", m_state );
    }

    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    if ( m_state == QC_OBJECT_STATE_INITIAL )
    {
        QC_ERROR( "SimulationNode already in initial state!" );
        status = QC_STATUS_BAD_STATE;
    }
    else if ( m_state == QC_OBJECT_STATE_RUNNING )
    {
        QC_ERROR( "SimulationNode in running state, stop it first!" );
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        // Clear registered buffers
        m_registeredBuffers.clear();

        // Reset state if not forced
        if ( !cfg.forceState )
        {
            m_state = QC_OBJECT_STATE_INITIAL;
        }

        // De-initialize the base node
        status2 = NodeBase::DeInitialize();
        if ( status2 != QC_STATUS_OK )
        {
            QC_ERROR( "Failed to de-initialize node base with status: %d", status2 );
            status = status2;
        }
    }

    return status;
}

/**
 * Get the current state of the node
 * @return Current node state
 */
QCObjectState_e SimulationNode::GetState()
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    // Check for configured return status
    auto it = cfg.returnStatusForFunctionMap.find( "GetState" );
    if ( it != cfg.returnStatusForFunctionMap.end() )
    {
        return QC_OBJECT_STATE_ERROR;
    }

    if ( cfg.forceState )
    {
        return cfg.forcedState;
    }

    return m_state;
}

QCNodeConfigIfs &SimulationNode::GetConfigurationIfs()
{
    return m_configIfs;
}

QCNodeMonitoringIfs &SimulationNode::GetMonitoringIfs()
{
    return m_monitorIfs;
}

/**
 * Process input buffers and copy data to output buffers
 * @param frameDesc Frame descriptor containing buffer information
 * @return Status of the buffer processing operation
 */
QCStatus_e SimulationNode::ProcessBuffers( QCFrameDescriptorNodeIfs &frameDesc )
{
    // Get configuration
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );
    QCStatus_e status = QC_STATUS_OK;

    for ( uint32_t i = 0; i < cfg.numInputs && i < cfg.numOutputs; i++ )
    {
        uint32_t inputBufferId = 0;
        uint32_t outputBufferId = 0;
        bool foundInput = false;
        bool foundOutput = false;

        // Find input and output buffer IDs from the global buffer map
        for ( const auto &entry : cfg.globalBufferIdMap )
        {
            if ( entry.name == "input" + std::to_string( i ) )
            {
                inputBufferId = entry.globalBufferId;
                foundInput = true;
            }
            else if ( entry.name == "output" + std::to_string( i ) )
            {
                outputBufferId = entry.globalBufferId;
                foundOutput = true;
            }
        }

        if ( !foundInput || !foundOutput )
        {
            QC_INFO( "Skipping buffer pair %u: input found: %d, output found: %d", i, foundInput,
                     foundOutput );
            continue;
        }

        QCBufferDescriptorBase_t &inputBuffer = frameDesc.GetBuffer( inputBufferId );
        QCBufferDescriptorBase_t &outputBuffer = frameDesc.GetBuffer( outputBufferId );

        if ( inputBuffer.type == QC_BUFFER_TYPE_MAX || outputBuffer.type == QC_BUFFER_TYPE_MAX )
        {
            QC_INFO( "Skipping invalid buffer types for pair %u", i );
            continue;
        }

        BufferDescriptor_t *pInputBuffer = dynamic_cast<BufferDescriptor_t *>( &inputBuffer );
        BufferDescriptor_t *pOutputBuffer = dynamic_cast<BufferDescriptor_t *>( &outputBuffer );

        pOutputBuffer->size = pInputBuffer->size;

        if ( pInputBuffer->pBuf && pOutputBuffer->pBuf )
        {
            size_t copySize = pInputBuffer->size;
            if ( copySize > pOutputBuffer->size )
            {
                QC_WARN( "Output buffer %u is smaller than input buffer, truncating copy", i );
                copySize = pOutputBuffer->size;
            }
            memcpy( pOutputBuffer->pBuf, pInputBuffer->pBuf, copySize );
        }
        else
        {
            QC_WARN( "Null buffer pointer detected for buffer pair %u", i );
        }
    }

    return status;
}

/**
 * Simulate different error conditions for testing
 * @param errorType Type of error to simulate
 * @return Status code corresponding to the simulated error
 */
QCStatus_e SimulationNode::SimulateError( SimulationNode_ErrorType_e errorType )
{
    // Get configuration for timeout values
    const SimulationNodeConfig_t &cfg =
            dynamic_cast<const SimulationNodeConfig_t &>( m_configIfs.Get() );

    switch ( errorType )
    {
        case SIMULATION_ERROR_DEADLOCK:
            QC_ERROR( "SimulationNode simulating deadlock for %u seconds", cfg.deadlockTimeoutSec );
            std::this_thread::sleep_for( std::chrono::seconds( cfg.deadlockTimeoutSec ) );
            return QC_STATUS_TIMEOUT;

        case SIMULATION_ERROR_TIMEOUT:
            QC_ERROR( "SimulationNode simulating timeout for %u seconds",
                      cfg.timeoutSimulationSec );
            std::this_thread::sleep_for( std::chrono::seconds( cfg.timeoutSimulationSec ) );
            return QC_STATUS_TIMEOUT;

        case SIMULATION_ERROR_BAD_ARGUMENTS:
            QC_ERROR( "SimulationNode simulating bad arguments error" );
            return QC_STATUS_BAD_ARGUMENTS;

        default:
            return QC_STATUS_OK;
    }
}

// Factory function implementation
QCNODE_SIMULATIONNODE_API QCNodeIfs *CreateSimulationNode()
{
    return new SimulationNode();
}

}   // namespace Node
}   // namespace QC