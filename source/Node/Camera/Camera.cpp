// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Camera.hpp"
#include "CameraImpl.hpp"
#include <new>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_QCX, Camera )

using namespace QC::Memory;

Camera::Camera()
    : m_pCamImpl( new ( std::nothrow ) CameraImpl( m_nodeId, m_logger ) ),
      m_configIfs( m_logger, m_pCamImpl ),
      m_monitor( m_logger, m_pCamImpl )
{
    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "Failed to allocate CameraImpl (out of memory)" );
    }
}

Camera::~Camera()
{
    if ( nullptr != m_pCamImpl )
    {
        delete m_pCamImpl;
        m_pCamImpl = nullptr;
    }
}

QCStatus_e Camera::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else
    {
        std::string errors;
        const QCNodeConfigBase_t &cfg = m_configIfs.Get();
        bool bNodeBaseInitDone = false;

        status = m_configIfs.VerifyAndSet( config.config, errors );
        if ( QC_STATUS_OK == status )
        {
            status = NodeBase::Init( cfg.nodeId );
        }
        else
        {
            QC_ERROR( "config error: %s", errors.c_str() );
        }

        if ( QC_STATUS_OK == status )
        {
            bNodeBaseInitDone = true;
            status = m_pCamImpl->Initialize( config.callback, config.buffers );
        }

        if ( QC_STATUS_OK != status )
        {
            if ( bNodeBaseInitDone )
            {
                (void) NodeBase::DeInitialize();
            }
        }
    }

    return status;
}

QCStatus_e Camera::DeInitialize()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else
    {
        QCStatus_e status2 = m_pCamImpl->DeInitialize();
        if ( QC_STATUS_OK != status2 )
        {
            status = status2;
            QC_ERROR( "Failed to deinitialize camera" );
        }

        status2 = NodeBase::DeInitialize();
        if ( QC_STATUS_OK != status2 )
        {
            status = status2;
            QC_ERROR( "Failed to deinitialize NodeBase" );
        }
    }

    return status;
}

QCStatus_e Camera::Start()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        status = m_pCamImpl->Start();
    }

    return status;
}

QCStatus_e Camera::Stop()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        status = m_pCamImpl->Stop();
    }

    return status;
}

QCStatus_e Camera::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        status = m_pCamImpl->ProcessFrameDescriptor( frameDesc );
    }

    return status;
}

QCObjectState_e Camera::GetState()
{
    QCObjectState_e state = QC_OBJECT_STATE_ERROR;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        state = m_pCamImpl->GetState();
    }

    return state;
}

}   // namespace Node
}   // namespace QC
