// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Voxelization.hpp"
#include "VoxelizationImpl.hpp"

namespace QC
{
namespace Node
{

QCStatus_e VoxelizationMonitor::VerifyAndSet( const std::string config, std::string &errors )
{
    return QC_STATUS_UNSUPPORTED;
}

QCStatus_e VoxelizationMonitor::GetOptions( std::string &options )
{
    return QC_STATUS_UNSUPPORTED;
}

const QCNodeMonitoringBase_t &VoxelizationMonitor::Get()
{
    return m_pVoxelImpl->GetMonitorConifg();
}

}   // namespace Node
}   // namespace QC
