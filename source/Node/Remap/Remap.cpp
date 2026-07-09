// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Remap.hpp"
#include "RemapImpl.hpp"
#include <new>
#include <unistd.h>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_FADAS_REMAP, Remap )

Remap::Remap()
    : m_pRemapImpl( new( std::nothrow ) RemapImpl( m_nodeId, m_logger ) ),
      m_configIfs( m_logger, m_pRemapImpl ),
      m_monitorIfs( m_logger, m_pRemapImpl )
{
    if ( nullptr == m_pRemapImpl )
    {
        QC_ERROR( "Failed to allocate RemapImpl (out of memory)" );
    }
}

Remap::~Remap()
{
    if ( nullptr != m_pRemapImpl )
    {
        delete m_pRemapImpl;
        m_pRemapImpl = nullptr;
    }
}

QCStatus_e Remap::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    bool bNodeBaseInitDone = false;

    if ( nullptr == m_pRemapImpl )
    {
        QC_ERROR( "RemapImpl not allocated (out of memory)" );
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
            status = m_pRemapImpl->Initialize( config.buffers );
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

QCStatus_e Remap::DeInitialize()
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    if ( nullptr == m_pRemapImpl )
    {
        status = QC_STATUS_NOMEM;
    }
    else
    {
        status2 = m_pRemapImpl->DeInitialize();
        if ( QC_STATUS_OK == status2 )
        {
            status = status2;
        }

        status2 = NodeBase::DeInitialize();
        if ( QC_STATUS_OK == status2 )
        {
            status = status2;
        }
    }

    return status;
}

QCStatus_e Remap::Start()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pRemapImpl )
    {
        status = m_pRemapImpl->Start();
    }

    return status;
}

QCStatus_e Remap::Stop()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pRemapImpl )
    {
        status = m_pRemapImpl->Stop();
    }

    return status;
}

QCStatus_e Remap::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pRemapImpl )
    {
        status = m_pRemapImpl->ProcessFrameDescriptor( frameDesc );
    }

    return status;
}

QCObjectState_e Remap::GetState()
{
    QCObjectState_e state = QC_OBJECT_STATE_ERROR;

    if ( nullptr != m_pRemapImpl )
    {
        state = m_pRemapImpl->GetState();
    }

    return state;
}

}   // namespace Node
}   // namespace QC
