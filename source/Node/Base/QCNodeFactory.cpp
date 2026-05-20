// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Ifs/QCNodeFactory.hpp"
#include "QC/Node/NodeBase.hpp"

#include <cassert>
#include <map>
#include <sstream>
#include <vector>

namespace QC
{
namespace Node
{

// ---------------------------------------------------------------------------
// File-scope registry
//
// GetNodeMap()  : returns reference to map of registered QCNodeType_e to factory function.
// GetNodeTypes(): returns reference to ordered list of registered types.
//
// Both containers are populated at static-initialisation time by the
// Register<ClassName> objects that are instantiated by REGISTER_NODE()
// macros placed in each node's translation unit.
//
// Using the "construct on first use" idiom to avoid static initialization order fiasco.
// ---------------------------------------------------------------------------
static std::map<QCNodeType_e, Node_CreateFunction_t> &GetNodeMap()
{
    static std::map<QCNodeType_e, Node_CreateFunction_t> s_NodeMap;
    return s_NodeMap;
}

static std::vector<QCNodeType_e> &GetNodeTypes()
{
    static std::vector<QCNodeType_e> s_NodeTypes;
    return s_NodeTypes;
}

// ---------------------------------------------------------------------------
// RegisterNode
//
// Free function declared in QCNodeRegister.hpp.
// Associates a QCNodeType_e key with a zero-argument factory function.
// Mirrors SampleIF::RegisterSample().
// Duplicate registrations trigger an assertion in debug builds.
// ---------------------------------------------------------------------------
void RegisterNode( QCNodeType_e type, Node_CreateFunction_t createFnc )
{
    auto &nodeMap = GetNodeMap();
    auto &nodeTypes = GetNodeTypes();

    auto it = nodeMap.find( type );
    if ( it == nodeMap.end() )
    {
        nodeMap[type] = createFnc;
        nodeTypes.push_back( type );
    }
    else
    {
        assert( 0 );   // duplicate registration for the same QCNodeType_e
    }
}

// ---------------------------------------------------------------------------
// CreateNode
//
// Looks up the registered factory function for the requested type and
// invokes it.  outNode is reset to nullptr when the type is not registered
// or when the factory function returns nullptr.
//
// ISO 26262:6 Table 8 §1a : single return statement at end of function.
// ISO 26262:6 Table 8 §1b : outNode is null on unsupported type.
// ---------------------------------------------------------------------------
QCStatus_e QCNodeFactory::CreateNode( QCNodeType_e type, std::unique_ptr<QCNodeIfs> &outNode )
{
    QCStatus_e status = QC_STATUS_OK;

    outNode.reset();

    auto &nodeMap = GetNodeMap();
    auto it = nodeMap.find( type );
    if ( it != nodeMap.end() )
    {
        Node_CreateFunction_t createFnc = it->second;
        outNode = createFnc();
        if ( !outNode )
        {
            status = QC_STATUS_FAIL;
        }
    }
    else
    {
        status = QC_STATUS_UNSUPPORTED;
    }

    return status;
}

// ---------------------------------------------------------------------------
// GetSupportedNodeTypes
//
// Returns the list of QCNodeType_e values that have been registered via
// REGISTER_NODE() in the current build.  The list is built incrementally
// by RegisterNode() and reflects exactly the set of types for which
// CreateNode() will succeed.
// ---------------------------------------------------------------------------
const std::vector<QCNodeType_e> &QCNodeFactory::GetSupportedNodeTypes()
{
    return GetNodeTypes();
}

// Returns JSON object of supported node types as key-value pairs (name: integer value)
std::string QCNodeFactory::GetSupportedNodeTypesJson()
{
    static const std::pair<QCNodeType_e, const char *> kNames[] = {
            { QC_NODE_TYPE_RESERVED, "QC_NODE_TYPE_RESERVED" },
            { QC_NODE_TYPE_QNN, "QC_NODE_TYPE_QNN" },
            { QC_NODE_TYPE_QCX, "QC_NODE_TYPE_QCX" },
            { QC_NODE_TYPE_FADAS_REMAP, "QC_NODE_TYPE_FADAS_REMAP" },
            { QC_NODE_TYPE_CL_2D_FLEX, "QC_NODE_TYPE_CL_2D_FLEX" },
            { QC_NODE_TYPE_GL_2D_FLEX, "QC_NODE_TYPE_GL_2D_FLEX" },
            { QC_NODE_TYPE_EVA_DFS, "QC_NODE_TYPE_EVA_DFS" },
            { QC_NODE_TYPE_EVA_OPTICAL_FLOW, "QC_NODE_TYPE_EVA_OPTICAL_FLOW" },
            { QC_NODE_TYPE_VENC, "QC_NODE_TYPE_VENC" },
            { QC_NODE_TYPE_VDEC, "QC_NODE_TYPE_VDEC" },
            { QC_NODE_TYPE_RADAR, "QC_NODE_TYPE_RADAR" },
            { QC_NODE_TYPE_VOXEL, "QC_NODE_TYPE_VOXEL" },
            { QC_NODE_TYPE_CUSTOM_0, "QC_NODE_TYPE_CUSTOM_0" },
            { QC_NODE_TYPE_CUSTOM_1, "QC_NODE_TYPE_CUSTOM_1" },
            { QC_NODE_TYPE_CUSTOM_2, "QC_NODE_TYPE_CUSTOM_2" },
            { QC_NODE_TYPE_CUSTOM_3, "QC_NODE_TYPE_CUSTOM_3" },
            { QC_NODE_TYPE_CUSTOM_4, "QC_NODE_TYPE_CUSTOM_4" },
    };

    const std::vector<QCNodeType_e> &supported = GetSupportedNodeTypes();

    std::string json = "{";
    bool first = true;

    for ( const QCNodeType_e type : supported )
    {
        for ( const auto &[enumVal, name] : kNames )
        {
            if ( enumVal == type )
            {
                if ( !first )
                {
                    json += ',';
                }
                json += '"';
                json += name;
                json += "\":";
                json += std::to_string( static_cast<int>( type ) );
                first = false;
                break;
            }
        }
    }

    json += '}';
    return json;
}

}   // namespace Node
}   // namespace QC
