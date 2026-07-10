// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/CL2DFlex.hpp"
#include "CL2DFlexImpl.hpp"
#include <new>
#include <unistd.h>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_CL_2D_FLEX, CL2DFlex )

CL2DFlex::CL2DFlex()
    : m_pCL2DFlexImpl( new( std::nothrow ) CL2DFlexImpl( m_nodeId, m_logger ) ),
      m_configIfs( m_logger, m_pCL2DFlexImpl ),
      m_monitorIfs( m_logger, m_pCL2DFlexImpl )
{
    if ( nullptr == m_pCL2DFlexImpl )
    {
        QC_ERROR( "Failed to allocate CL2DFlexImpl (out of memory)" );
    }
}

CL2DFlex::~CL2DFlex()
{
    if ( nullptr != m_pCL2DFlexImpl )
    {
        delete m_pCL2DFlexImpl;
        m_pCL2DFlexImpl = nullptr;
    }
}

QCStatus_e CL2DFlex::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    bool bNodeBaseInitDone = false;

    if ( nullptr == m_pCL2DFlexImpl )
    {
        QC_ERROR( "CL2DFlexImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else
    {
        const QCNodeConfigBase_t &cfg = m_configIfs.Get();

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
            status = m_pCL2DFlexImpl->Initialize( config.buffers );
        }

        if ( QC_STATUS_OK != status )
        { /* do error clean up */
            if ( bNodeBaseInitDone )
            {
                (void) NodeBase::DeInitialize();
            }
        }
    }

    return status;
}

QCStatus_e CL2DFlex::DeInitialize()
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    if ( nullptr == m_pCL2DFlexImpl )
    {
        status = QC_STATUS_NOMEM;
    }
    else
    {
        status2 = m_pCL2DFlexImpl->DeInitialize();
        if ( QC_STATUS_OK != status2 )
        {
            status = status2;
        }

        status2 = NodeBase::DeInitialize();
        if ( QC_STATUS_OK != status2 )
        {
            status = status2;
        }
    }

    return status;
}

QCStatus_e CL2DFlex::Start()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pCL2DFlexImpl )
    {
        status = m_pCL2DFlexImpl->Start();
    }

    return status;
}

QCStatus_e CL2DFlex::Stop()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pCL2DFlexImpl )
    {
        status = m_pCL2DFlexImpl->Stop();
    }

    return status;
}

QCStatus_e CL2DFlex::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pCL2DFlexImpl )
    {
        status = m_pCL2DFlexImpl->ProcessFrameDescriptor( frameDesc );
    }

    return status;
}

QCObjectState_e CL2DFlex::GetState()
{
    QCObjectState_e state = QC_OBJECT_STATE_ERROR;

    if ( nullptr != m_pCL2DFlexImpl )
    {
        state = m_pCL2DFlexImpl->GetState();
    }

    return state;
}

}   // namespace Node
}   // namespace QC
