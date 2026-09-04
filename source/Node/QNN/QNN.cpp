// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/QNN.hpp"
#include "QnnImpl.hpp"
#include <new>
#include <unistd.h>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_QNN, Qnn )

using namespace QC::Memory;

Qnn::Qnn()
    : m_pQnnImpl( new( std::nothrow ) QnnImpl( m_nodeId, m_logger ) ),
      m_configIfs( m_logger, m_pQnnImpl ),
      m_monitorIfs( m_logger, m_pQnnImpl )
{
    if ( nullptr == m_pQnnImpl )
    {
        QC_ERROR( "Failed to allocate QnnImpl (out of memory)" );
    }
}

Qnn::~Qnn()
{
    if ( nullptr != m_pQnnImpl )
    {
        delete m_pQnnImpl;
        m_pQnnImpl = nullptr;
    }
}

QCStatus_e Qnn::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    bool bNodeBaseInitDone = false;

    if ( nullptr == m_pQnnImpl )
    {
        QC_ERROR( "QnnImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else if ( QC_OBJECT_STATE_INITIAL != GetState() )
    {
        QC_ERROR( "QNN not in initial state!" );
        status = QC_STATUS_BAD_STATE;
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
            status = m_pQnnImpl->Initialize( config.callback, config.buffers );
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

QCStatus_e Qnn::DeInitialize()
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    if ( nullptr == m_pQnnImpl )
    {
        status = QC_STATUS_NOMEM;
    }
    else
    {
        status2 = m_pQnnImpl->DeInitialize();
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

QCStatus_e Qnn::Start()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pQnnImpl )
    {
        status = m_pQnnImpl->Start();
    }

    return status;
}

QCStatus_e Qnn::Stop()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pQnnImpl )
    {
        status = m_pQnnImpl->Stop();
    }

    return status;
}

QCStatus_e Qnn::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pQnnImpl )
    {
        status = m_pQnnImpl->ProcessFrameDescriptor( frameDesc );
    }

    return status;
}

QCObjectState_e Qnn::GetState()
{
    QCObjectState_e state = QC_OBJECT_STATE_ERROR;

    if ( nullptr != m_pQnnImpl )
    {
        state = m_pQnnImpl->GetState();
    }

    return state;
}

}   // namespace Node
}   // namespace QC
