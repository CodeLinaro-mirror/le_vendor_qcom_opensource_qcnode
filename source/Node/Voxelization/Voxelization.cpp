// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Voxelization.hpp"
#include "VoxelizationImpl.hpp"
#include <new>

namespace QC
{
namespace Node
{

REGISTER_NODE( QC_NODE_TYPE_VOXEL, Voxelization )

using namespace QC::Memory;

Voxelization::Voxelization()
    : m_pVoxelImpl( new( std::nothrow ) VoxelizationImpl( m_nodeId, m_logger ) ),
      m_configIfs( m_logger, m_pVoxelImpl ),
      m_monitor( m_logger, m_pVoxelImpl )
{
    if ( nullptr == m_pVoxelImpl )
    {
        QC_ERROR( "Failed to allocate VoxelizationImpl (out of memory)" );
    }
}

Voxelization::~Voxelization()
{
    if ( nullptr != m_pVoxelImpl )
    {
        delete m_pVoxelImpl;
        m_pVoxelImpl = nullptr;
    }
}

QCStatus_e Voxelization::Initialize( QCNodeInit_t &config )
{
    QCStatus_e ret = QC_STATUS_OK;

    std::string errors;
    bool bNodeBaseInitDone = false;

    if ( nullptr == m_pVoxelImpl )
    {
        QC_ERROR( "VoxelizationImpl not allocated (out of memory)" );
        ret = QC_STATUS_NOMEM;
    }
    else
    {
        const QCNodeConfigBase_t &cfg = m_configIfs.Get();

        ret = m_configIfs.VerifyAndSet( config.config, errors );
        if ( QC_STATUS_OK == ret )
        {
            ret = NodeBase::Init( cfg.nodeId );
        }
        else
        {
            QC_ERROR( "config error: %s", errors.c_str() );
        }

        if ( QC_STATUS_OK == ret )
        {
            bNodeBaseInitDone = true;
            ret = m_pVoxelImpl->Initialize( config.callback, config.buffers );
        }

        if ( QC_STATUS_OK != ret )
        {
            if ( bNodeBaseInitDone )
            {
                (void) NodeBase::DeInitialize();
            }
        }
    }

    return ret;
}

QCStatus_e Voxelization::DeInitialize()
{
    QCStatus_e ret = QC_STATUS_OK;
    QCStatus_e ret2;

    if ( nullptr == m_pVoxelImpl )
    {
        ret = QC_STATUS_NOMEM;
    }
    else
    {
        ret2 = m_pVoxelImpl->DeInitialize();
        if ( QC_STATUS_OK != ret2 )
        {
            ret = ret2;
            QC_ERROR( "Failed to deinitialize voxelization" );
        }

        ret2 = NodeBase::DeInitialize();
        if ( QC_STATUS_OK != ret2 )
        {
            ret = ret2;
            QC_ERROR( "Failed to deinitialize NodeBase" );
        }
    }

    return ret;
}

QCStatus_e Voxelization::Start()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pVoxelImpl )
    {
        status = m_pVoxelImpl->Start();
    }

    return status;
}

QCStatus_e Voxelization::Stop()
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pVoxelImpl )
    {
        status = m_pVoxelImpl->Stop();
    }

    return status;
}

QCStatus_e Voxelization::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_NOMEM;

    if ( nullptr != m_pVoxelImpl )
    {
        status = m_pVoxelImpl->ProcessFrameDescriptor( frameDesc );
    }

    return status;
}

QCObjectState_e Voxelization::GetState()
{
    QCObjectState_e state = QC_OBJECT_STATE_ERROR;

    if ( nullptr != m_pVoxelImpl )
    {
        state = m_pVoxelImpl->GetState();
    }

    return state;
}

}   // namespace Node
}   // namespace QC
