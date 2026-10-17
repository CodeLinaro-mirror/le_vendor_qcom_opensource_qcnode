// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/NodeFrameDescriptor.hpp"

#include "QC/Node/Ifs/QCNodeFrameDescriptorFactory.hpp"

namespace QC
{
namespace Node
{

QCDummyBufferDescriptor_t NodeFrameDescriptor::s_dummy;

// ---------------------------------------------------------------------------
// Register the default NodeFrameDescriptor implementation with
// QCNodeFrameDescriptorFactory.  This makes
//   QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
//       QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, numOfBuffers, out )
// functional in every build that links QCNodeBase, not just in unit tests.
//
// The macro is invoked inside the QC::Node namespace so that the unqualified
// class name NodeFrameDescriptor is visible for token pasting.
// ---------------------------------------------------------------------------
REGISTER_FRAME_DESCRIPTOR( QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, NodeFrameDescriptor )

}   // namespace Node
}   // namespace QC
