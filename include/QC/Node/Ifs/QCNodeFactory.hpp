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
//   CreateNode            — selected by QCNodeType_e
//   GetSupportedNodeTypes — populates a vector of types constructible on this build
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

    // Populates outTypes with node types constructible via CreateNode() in this build.
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedNodeTypes( std::vector<QCNodeType_e> &outTypes );

    // Populates outJson with a JSON object of node types constructible via CreateNode().
    // Returns QC_STATUS_OK on success; outJson is valid only on QC_STATUS_OK.
    static QCStatus_e GetSupportedNodeTypesJson( std::string &outJson );

private:
    QCNodeFactory() = delete;
};

}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_FACTORY_HPP
