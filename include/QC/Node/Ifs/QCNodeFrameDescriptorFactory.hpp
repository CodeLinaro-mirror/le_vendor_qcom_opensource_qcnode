// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP
#define QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "QC/Common/QCDefs.hpp"
#include "QC/Node/Ifs/QCFrameDescriptorNodeIfs.hpp"
#include "QC/Node/Ifs/QCNodeDefs.hpp"

namespace QC
{
namespace Node
{

// ---------------------------------------------------------------------------
// QCNodeFrameDescriptorFactory
//
// Static factory for QCFrameDescriptorNodeIfs implementations:
//
//   CreateFrameDescriptor                — creates an implementation selected
//                                          by QCNodeFrameDescType_e
//   GetSupportedNodeFrameDescriptorTypes — populates the types supported by
//                                          this build
//
// ISO 26262:6 Table 8 §1b : out-parameter is null on error.
// ---------------------------------------------------------------------------
class QCNodeFrameDescriptorFactory
{
public:
    // Creates a frame descriptor for the given type.
    // Returns QC_STATUS_UNSUPPORTED if the type is not supported; outDescriptor is null.
    // Returns QC_STATUS_BAD_ARGUMENTS if numOfBuffers is 0; outDescriptor is null.
    // Returns QC_STATUS_FAIL on allocation failure; outDescriptor is null.
    static QCStatus_e
    CreateFrameDescriptor( QCNodeFrameDescType_e type, uint32_t numOfBuffers,
                           std::unique_ptr<QCFrameDescriptorNodeIfs> &outDescriptor );

    // Populates outTypes with frame descriptor types supported by this build.
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e
    GetSupportedNodeFrameDescriptorTypes( std::vector<QCNodeFrameDescType_e> &outTypes );

private:
    QCNodeFrameDescriptorFactory() = delete;
};

}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP
