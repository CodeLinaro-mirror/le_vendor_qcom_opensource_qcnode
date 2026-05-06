// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/SimulationNode.hpp"
#include <cstring>
#include <unistd.h>

// TODO: Need to update this class
namespace QC
{
namespace Node
{

SimulationNodeMonitoring::SimulationNodeMonitoring( Logger &logger ) : m_logger( logger ) {}

QCStatus_e SimulationNodeMonitoring::VerifyAndSet( const std::string config, std::string &errors )
{
    return QC_STATUS_OK;
}

const std::string &SimulationNodeMonitoring::GetOptions()
{
    if ( m_options.empty() )
    {
        DataTree dt;
        dt.Set<uint32_t>( "version", QCNODE_SIMULATION_NODE_VERSION );
        dt.Set<std::string>( "description", "SimulationNode monitoring options" );

        // Add monitoring options
        dt.Set<std::string>( "bEnableErrorReporting", "Enable error reporting" );
        dt.Set<std::string>( "processingTimeMs",
                             "Average processing time per frame in milliseconds" );
        dt.Set<std::string>( "frameCount", "Number of frames processed" );
        dt.Set<std::string>( "errorCount", "Number of errors encountered" );

        m_options = dt.Dump();
    }
    return m_options;
}

const QCNodeMonitoringBase_t &SimulationNodeMonitoring::Get()
{
    return m_monitorData;
}

uint32_t SimulationNodeMonitoring::GetMaximalSize()
{
    return sizeof( SimulationNodeMonitorConfig_t );
}

uint32_t SimulationNodeMonitoring::GetCurrentSize()
{
    return sizeof( SimulationNodeMonitorConfig_t );
}

QCStatus_e SimulationNodeMonitoring::Place( void *ptr, uint32_t &size )
{
    if ( ptr == nullptr )
    {
        return QC_STATUS_BAD_ARGUMENTS;
    }

    if ( size < sizeof( SimulationNodeMonitorConfig_t ) )
    {
        size = sizeof( SimulationNodeMonitorConfig_t );
        return QC_STATUS_OUT_OF_BOUND;
    }

    memcpy( ptr, &m_monitorData, sizeof( SimulationNodeMonitorConfig_t ) );
    size = sizeof( SimulationNodeMonitorConfig_t );

    return QC_STATUS_OK;
}

}   // namespace Node
}   // namespace QC