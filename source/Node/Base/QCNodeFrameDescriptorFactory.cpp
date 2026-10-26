// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Ifs/QCNodeFrameDescriptorFactory.hpp"

#include "QC/Node/NodeFrameDescriptor.hpp"

namespace QC
{
namespace Node
{

QCStatus_e QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
        QCNodeFrameDescType_e type, uint32_t numOfBuffers,
        std::unique_ptr<QCFrameDescriptorNodeIfs> &outDescriptor )
{
    QCStatus_e status = QC_STATUS_OK;

    outDescriptor.reset();

    if ( 0U == numOfBuffers )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        switch ( type )
        {
            case QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR:
                outDescriptor = std::make_unique<NodeFrameDescriptor>( numOfBuffers );
                break;

            default:
                status = QC_STATUS_UNSUPPORTED;
                break;
        }

        if ( ( QC_STATUS_OK == status ) && ( nullptr == outDescriptor ) )
        {
            status = QC_STATUS_FAIL;
        }
    }

    return status;
}

QCStatus_e QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes(
        std::vector<QCNodeFrameDescType_e> &outTypes )
{
    outTypes.clear();
    outTypes.push_back( QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR );

    return QC_STATUS_OK;
}

}   // namespace Node
}   // namespace QC
