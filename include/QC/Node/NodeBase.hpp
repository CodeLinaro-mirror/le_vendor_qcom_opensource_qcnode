// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_NODE_BASE_HPP
#define QC_NODE_BASE_HPP

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "QC/Common/DataTree.hpp"
#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/BufferDescriptor.hpp"
#include "QC/Infras/Memory/Ifs/QCBufferDescriptorBase.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryDefs.hpp"
#include "QC/Infras/Memory/ImageDescriptor.hpp"
#include "QC/Infras/Memory/TensorDescriptor.hpp"
#include "QC/Node/Ifs/QCFrameDescriptorNodeIfs.hpp"
#include "QC/Node/Ifs/QCNodeDefs.hpp"
#include "QC/Node/Ifs/QCNodeFactory.hpp"
#include "QC/Node/Ifs/QCNodeIfs.hpp"
#include "QC/Node/NodeConfigBase.hpp"
#include "QC/Node/NodeFrameDescriptor.hpp"

namespace QC
{
namespace Node
{

using namespace QC::Memory;

// ---------------------------------------------------------------------------
// Node_CreateFunction_t
//
// Signature of the zero-argument factory function used by NodeRegistrar.
// ---------------------------------------------------------------------------
typedef std::unique_ptr<QCNodeIfs> ( *Node_CreateFunction_t )();

class NodeBase : public QCNodeIfs
{
public:
    /**
     * @brief NodeBase Constructor.
     * @return None.
     */
    NodeBase() = default;
    NodeBase( const NodeBase & ) = default;
    NodeBase& operator=( const NodeBase& ) = default;

    /**
     * @brief Initializes Node.
     * @param[in] config The Node configuration.
     * @return QC_STATUS_OK on success, or an error code on failure.
     */
    virtual QCStatus_e Initialize( QCNodeInit_t &config ) = 0;

    /**
     * @brief Get the Node configuration interface.
     * @return A reference to the Node configuration interface.
     */
    virtual QCNodeConfigIfs &GetConfigurationIfs() = 0;

    /**
     * @brief Get the Node monitoring interface.
     * @return A reference to the Node monitoring interface.
     */
    virtual QCNodeMonitoringIfs &GetMonitoringIfs() = 0;

    /**
     * @brief Start the Node
     * @return QC_STATUS_OK on success, others on failure
     */
    virtual QCStatus_e Start() = 0;

    /**
     * @brief Processes the Frame Descriptor.
     * @param[in] frameDesc The frame descriptor containing a vector of input/output buffers.
     * @return QC_STATUS_OK on success, or an error code on failure.
     */
    virtual QCStatus_e ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc ) = 0;

    /**
     * @brief Stop the Node
     * @return QC_STATUS_OK on success, others on failure
     */
    virtual QCStatus_e Stop() = 0;

    /**
     * @brief De-initialize Node.
     * @return QC_STATUS_OK on success, others on failure
     */
    virtual QCStatus_e DeInitialize() = 0;

    /**
     * @brief Get the current state of the Node
     * @return The current state of the Node
     */
    virtual QCObjectState_e GetState() = 0;


protected:
    /**
     * @brief NodeBase Destructor (protected; not polymorphically deleted as NodeBase).
     * @return None
     */
    ~NodeBase() = default;

    /**
     * @brief Initialize the Node
     * @param[in] nodeId the Node unique ID
     * @param[in] level the logger message level
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Init( QCNodeID_t nodeId, Logger_Level_e level = LOGGER_LEVEL_ERROR );

protected:
    QCNodeID_t m_nodeId;
    Logger m_logger;
};

// ---------------------------------------------------------------------------
// RegisterNode
//
// Associates a QCNodeType_e key with a zero-argument factory function so
// that QCNodeFactory::CreateNode() can instantiate the corresponding node.
// Implemented in QCNodeFactory.cpp alongside the file-scope registry.
// Duplicate registrations trigger an assertion in debug builds.
// ---------------------------------------------------------------------------
void RegisterNode( QCNodeType_e type, Node_CreateFunction_t createFnc );

// ---------------------------------------------------------------------------
// NodeRegistrar<NodeClass>
//
// Registers a QCNodeIfs-derived class with QCNodeFactory so that
// QCNodeFactory::CreateNode( type, ... ) can instantiate it.
//
// A file-scope NodeRegistrar<NodeClass> object is created in each node
// implementation. Its constructor associates the QCNodeType_e key with the
// type-specific factory function during static initialization, before main().
//
// ISO 26262:6 Table 8 §1b : outNode is null when type is not registered.
// ---------------------------------------------------------------------------
template <typename NodeClass>
std::unique_ptr<QCNodeIfs> CreateRegisteredNode()
{
    return std::make_unique<NodeClass>();
}

template <typename NodeClass>
class NodeRegistrar final
{
public:
    explicit NodeRegistrar( QCNodeType_e type ) noexcept
    {
        RegisterNode( type, &CreateRegisteredNode<NodeClass> );
    }

    NodeRegistrar( const NodeRegistrar& ) = delete;
    NodeRegistrar& operator=( const NodeRegistrar& ) = delete;
};


}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_BASE_HPP
