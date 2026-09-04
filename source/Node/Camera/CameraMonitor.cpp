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
    static const CameraImplMonitorConfig_t s_defaultMonitorConfig{};
    const QCNodeMonitoringBase_t *pMonitorConfig = &s_defaultMonitorConfig;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        pMonitorConfig = &m_pCamImpl->GetMonitorConifg();
    }

    return *pMonitorConfig;
}

}   // namespace Node
}   // namespace QC
