// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file SimulationNode.hpp
 * @brief Defines a configurable simulation node implementation for testing and simulation
 *
 * This file contains the SimulationNode class which can be used to simulate various node
 * behaviors including error conditions, processing delays, and state transitions.
 */

#ifndef QC_NODE_SIMULATION_NODE_HPP
#define QC_NODE_SIMULATION_NODE_HPP

#include "QC/Node/NodeBase.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace QC
{
namespace Node
{

// Define export macros for shared library
#define QCNODE_SIMULATIONNODE_API __attribute__( ( visibility( "default" ) ) )

/** @brief The QCNode SimulationNode Version */
#define QCNODE_SIMULATION_NODE_VERSION_MAJOR 1U
#define QCNODE_SIMULATION_NODE_VERSION_MINOR 0U
#define QCNODE_SIMULATION_NODE_VERSION_PATCH 0U

#define QCNODE_SIMULATION_NODE_VERSION                                                             \
    ( ( QCNODE_SIMULATION_NODE_VERSION_MAJOR << 16U ) |                                            \
      ( QCNODE_SIMULATION_NODE_VERSION_MINOR << 8U ) | QCNODE_SIMULATION_NODE_VERSION_PATCH )

/**
 * @brief SimulationNode error simulation types
 *
 * Defines the types of errors that can be simulated by the SimulationNode
 */
typedef enum
{
    SIMULATION_ERROR_NONE = 0,      /**< No errors */
    SIMULATION_ERROR_DEADLOCK,      /**< Simulate deadlock */
    SIMULATION_ERROR_TIMEOUT,       /**< Simulate timeout */
    SIMULATION_ERROR_BAD_ARGUMENTS, /**< Return bad arguments error */
} SimulationNode_ErrorType_e;

/**
 * @brief SimulationNode processing mode
 *
 * Defines whether the node processes frames synchronously or asynchronously
 */
typedef enum
{
    SIMULATION_PROCESSING_SYNC = 0, /**< Synchronous processing */
    SIMULATION_PROCESSING_ASYNC     /**< Asynchronous processing */
} SimulationNode_ProcessingMode_e;

/**
 * @brief SimulationNode Configuration Data Structure
 */
typedef struct SimulationNodeConfig_s : public QCNodeConfigBase_t
{
    std::string name; /**< Node name */
    uint32_t id;      /**< Node ID */
    std::map<std::string, int>
            returnStatusForFunctionMap;             /**< Return code for APIs (e.g.,
                                                       SimulationNode::Initialize:QC_STATUS_FAIL) */
    SimulationNode_ProcessingMode_e processingMode; /**< Processing mode (sync/async) */
    uint32_t processingDelayMs;           /**< Simulated processing delay in milliseconds */
    uint32_t numInputs;                   /**< Number of input buffers */
    uint32_t numOutputs;                  /**< Number of output buffers */
    SimulationNode_ErrorType_e errorType; /**< Type of error to simulate */
    uint32_t errorRate;              /**< Error rate: after how many frames to simulate errors */
    uint32_t deadlockTimeoutSec;     /**< Timeout in seconds for deadlock simulation */
    uint32_t timeoutSimulationSec;   /**< Timeout in seconds for timeout simulation */
    bool forceState;                 /**< Whether to force a specific state */
    QCObjectState_e forcedState;     /**< State to force if forceState is true */
    std::vector<uint32_t> bufferIds; /**< Indices of buffers in QCNodeInit::buffers */
    std::vector<QCNodeBufferMapEntry_t>
            globalBufferIdMap; /**< Maps buffers in QCFrameDescriptorNodeIfs to inputs/outputs */
    bool bDeRegisterAllBuffersWhenStop; /**< Whether to deregister all buffers when stopped */
} SimulationNodeConfig_t;

/**
 * @brief SimulationNode Monitoring Configuration Data Structure
 */
typedef struct SimulationNodeMonitor_s : public QCNodeMonitoringBase_t
{
    bool bEnableErrorReporting; /**< Enable error reporting */
    uint32_t processingTimeMs;  /**< Time taken to process a frame */
    uint32_t errorCount;        /**< Number of errors encountered */
    uint32_t frameCount;        /**< Number of frames processed */
} SimulationNodeMonitorConfig_t;

/**
 * @brief SimulationNode configuration class
 *
 * Handles configuration verification and management for the SimulationNode
 */
class QCNODE_SIMULATIONNODE_API SimulationNodeConfig final : public NodeConfigBase
{
public:
    /**
     * @brief SimulationNodeConfig Constructor
     * @param[in] logger A reference to the logger to be shared and used by SimulationNodeConfig.
     * @return None
     */
    explicit SimulationNodeConfig( Logger &logger );

    /**
     * @brief SimulationNodeConfig Destructor
     * @return None
     */
    ~SimulationNodeConfig() = default;

    // Delete default copy and move constructors and assignment operators
    SimulationNodeConfig( const SimulationNodeConfig & ) = delete;
    SimulationNodeConfig &operator=( const SimulationNodeConfig & ) = delete;
    SimulationNodeConfig( SimulationNodeConfig && ) = delete;
    SimulationNodeConfig &operator=( SimulationNodeConfig && ) = delete;

    /**
     * @brief Verify the configuration string and set the configuration structure.
     * @param[in] config The configuration string.
     * @param[out] errors The error string returned if there is an error.
     * @return QC_STATUS_OK on success, other values on failure.
     */
    QCStatus_e VerifyAndSet( const std::string config, std::string &errors ) override;

    /**
     * @brief Get Configuration Options
     * @return A reference string to the JSON configuration options.
     */
    const std::string &GetOptions() override;

    /**
     * @brief Get the Configuration Structure.
     * @return A reference to the Configuration Structure.
     */
    const QCNodeConfigBase_t &Get() override;

private:
    /**
     * @brief Verify the static configuration section
     * @param[in] dt The data tree containing the configuration
     * @param[out] errors Error messages if verification fails
     * @return QC_STATUS_OK on success, other values on failure
     */
    QCStatus_e VerifyStaticConfig( DataTree &dt, std::string &errors );

    /**
     * @brief Parse the static configuration section
     * @param[in] dt The data tree containing the configuration
     * @param[out] errors Error messages if parsing fails
     * @return QC_STATUS_OK on success, other values on failure
     */
    QCStatus_e ParseStaticConfig( DataTree &dt, std::string &errors );

    /**
     * @brief Set up the global buffer ID map
     * @return QC_STATUS_OK on success, error code otherwise
     */
    QCStatus_e SetupGlobalBufferIdMap();

private:
    SimulationNodeConfig_t m_config; /**< Configuration data */
    std::string m_options;           /**< Configuration options string */
};

/**
 * @brief SimulationNode monitoring class
 *
 * Handles monitoring functionality for the SimulationNode
 */
class QCNODE_SIMULATIONNODE_API SimulationNodeMonitoring final : public QCNodeMonitoringIfs
{
public:
    /**
     * @brief SimulationNodeMonitoring Constructor
     * @param[in] logger A reference to the logger to be shared and used by
     * SimulationNodeMonitoring.
     * @return None
     */
    explicit SimulationNodeMonitoring( Logger &logger );

    /**
     * @brief SimulationNodeMonitoring Destructor
     * @return None
     */
    ~SimulationNodeMonitoring() = default;

    // Delete default copy and move constructors and assignment operators
    SimulationNodeMonitoring( const SimulationNodeMonitoring & ) = delete;
    SimulationNodeMonitoring &operator=( const SimulationNodeMonitoring & ) = delete;
    SimulationNodeMonitoring( SimulationNodeMonitoring && ) = delete;
    SimulationNodeMonitoring &operator=( SimulationNodeMonitoring && ) = delete;

    /**
     * @brief Verify and set monitoring configuration
     * @param[in] config Configuration string
     * @param[out] errors Error messages if verification fails
     * @return QC_STATUS_OK on success, other values on failure
     */
    QCStatus_e VerifyAndSet( const std::string config, std::string &errors ) override;

    /**
     * @brief Get monitoring options
     * @return Reference to options string
     */
    const std::string &GetOptions() override;

    /**
     * @brief Get monitoring configuration
     * @return Reference to monitoring configuration
     */
    const QCNodeMonitoringBase_t &Get() override;

    /**
     * @brief Get maximum monitoring data size
     * @return Maximum size in bytes
     */
    uint32_t GetMaximalSize() override;

    /**
     * @brief Get current monitoring data size
     * @return Current size in bytes
     */
    uint32_t GetCurrentSize() override;

    /**
     * @brief Place monitoring data in provided buffer
     * @param[out] ptr Buffer to place data
     * @param[in,out] size Size of buffer/data
     * @return QC_STATUS_OK on success, error code otherwise
     */
    QCStatus_e Place( void *ptr, uint32_t &size ) override;

private:
    SimulationNodeMonitorConfig_t m_monitorData; /**< Monitoring data */
    Logger &m_logger;                            /**< Reference to logger */
    std::string m_options;                       /**< Configuration options string */
};

/**
 * @brief SimulationNode class
 *
 * This class implements a simulation node that can simulate various node behaviors
 * based on configuration. It can be used for testing node interactions,
 * error handling, and performance characteristics.
 */
class QCNODE_SIMULATIONNODE_API SimulationNode final : public NodeBase
{
public:
    /**
     * @brief SimulationNode Constructor
     * @return None
     */
    SimulationNode();

    /**
     * @brief SimulationNode Destructor
     * @return None
     */
    ~SimulationNode();

    // Delete default copy and move constructors and assignment operators
    SimulationNode( const SimulationNode & ) = delete;
    SimulationNode &operator=( const SimulationNode & ) = delete;
    SimulationNode( SimulationNode && ) = delete;
    SimulationNode &operator=( SimulationNode && ) = delete;

    /**
     * @brief Initializes Node SimulationNode.
     * @param[in] config The Node SimulationNode configuration.
     * @return QC_STATUS_OK on success, or an error code on failure.
     */
    QCStatus_e Initialize( QCNodeInit_t &config ) override;

    /**
     * @brief Get the Node SimulationNode configuration interface.
     * @return A reference to the Node SimulationNode configuration interface.
     */
    QCNodeConfigIfs &GetConfigurationIfs() override;

    /**
     * @brief Get the Node SimulationNode monitoring interface.
     * @return A reference to the Node SimulationNode monitoring interface.
     */
    QCNodeMonitoringIfs &GetMonitoringIfs() override;

    /**
     * @brief Start the Node SimulationNode
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Start() override;

    /**
     * @brief Processes the Frame Descriptor.
     * @param[in] frameDesc The frame descriptor containing input/output buffers.
     * @return QC_STATUS_OK on success, or an error code on failure.
     */
    QCStatus_e ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc ) override;

    /**
     * @brief Stop the Node SimulationNode
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Stop() override;

    /**
     * @brief De-initialize Node SimulationNode
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e DeInitialize() override;

    /**
     * @brief Get the current state of the Node SimulationNode
     * @return The current state of the Node SimulationNode
     */
    QCObjectState_e GetState() override;

private:
    /**
     * @brief Process input/output buffers
     * @param frameDesc Frame descriptor containing buffers
     * @return QC_STATUS_OK on success, error code otherwise
     */
    QCStatus_e ProcessBuffers( QCFrameDescriptorNodeIfs &frameDesc );

    /**
     * @brief Simulate a specific error
     * @param errorType Type of error to simulate
     * @return Status code corresponding to the simulated error
     */
    QCStatus_e SimulateError( SimulationNode_ErrorType_e errorType );

private:
    SimulationNodeConfig m_configIfs;      /**< Configuration interface */
    SimulationNodeMonitoring m_monitorIfs; /**< Monitoring interface */
    QCObjectState_e m_state;               /**< Current state */
    QCNodeEventCallBack_t m_callback;      /**< Event callback */
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>>
            m_registeredBuffers; /**< Registered buffers */
    uint32_t m_frameCount;       /**< Number of frames processed */
};

/**
 * @brief Factory function to create a SimulationNode instance
 * @return Pointer to created SimulationNode instance
 */
QCNODE_SIMULATIONNODE_API QCNodeIfs *CreateSimulationNode();

}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_SIMULATION_NODE_HPP