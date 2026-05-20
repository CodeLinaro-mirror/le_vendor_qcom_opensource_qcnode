// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_FACTORY_HPP
#define QC_NODE_FACTORY_HPP

#include <memory>
#include <vector>

#include "QC/Common/QCDefs.hpp"
#include "QC/Node/Ifs/QCNodeIfs.hpp"

namespace QC
{
namespace Node
{

// ---------------------------------------------------------------------------
// QCNodeFactory
//
// Static factory methods for QCNodeIfs, following the same convention as
// QCMemoryFactory:
//
//   CreateNode          — selected by QCNodeType_e
//   GetSupportedNodeTypes — returns the types constructible on this build
//
// CreateNode returns QCStatus_e and writes the constructed node into the
// caller-supplied unique_ptr out-parameter. The out-parameter is reset to
// nullptr on failure.
//
// ISO 26262:6 Table 8 §1b : out-parameter is null on unsupported type.
// ---------------------------------------------------------------------------
class QCNodeFactory
{
public:
    // Returns the node implementation for the given type.
    // Returns QC_STATUS_UNSUPPORTED for unrecognised types; outNode is null.
    static QCStatus_e CreateNode( QCNodeType_e type, std::unique_ptr<QCNodeIfs> &outNode );

    // Returns the node types constructible via CreateNode() in this build.
    static const std::vector<QCNodeType_e> &GetSupportedNodeTypes();

    // Returns the node types json
    static std::string GetSupportedNodeTypesJson();

private:
    QCNodeFactory() = delete;
};

}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_FACTORY_HPP
