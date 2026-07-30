// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "CameraImpl.hpp"
#include "QC/Node/Camera.hpp"

namespace QC
{
namespace Node
{

QCStatus_e CameraMonitor::VerifyAndSet( const std::string config, std::string &errors )
{
    return QC_STATUS_UNSUPPORTED;
}

QCStatus_e CameraMonitor::GetOptions( std::string &options )
{
    return QC_STATUS_UNSUPPORTED;
}

const QCNodeMonitoringBase_t &CameraMonitor::Get()
{
    return m_pCamImpl->GetMonitorConifg();
}

}   // namespace Node
}   // namespace QC
