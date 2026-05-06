// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


/**
 * @file HeteroSchedulerWrapper.hpp
 * @brief Utility class for registering objects with the HeteroScheduler.
 *
 * This class provides a simplified interface for applications that need to
 * register multiple objects with the HeteroScheduler. It encapsulates the
 * common registration pattern and lifecycle management.
 */

#ifndef QC_HETERO_SCHEDULER_WRAPPER_HPP
#define QC_HETERO_SCHEDULER_WRAPPER_HPP

#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "cf_orchestrator.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace QC
{
namespace sample
{

/**
 * @class HeteroSchedulerWrapper
 * @brief Utility class for managing HeteroScheduler client registration.
 *
 * This class simplifies the process of registering with the HeteroScheduler by:
 * - Managing the initialization and deinitialization lifecycle
 * - Loading and configuring schedules
 * - Creating and managing ThreadPools
 * - Registering vertex callbacks
 * - Starting and stopping client execution
 *
 * Typical usage:
 * @code
 * HeteroSchedulerWrapper client("myClient", "schedule.xml");
 * if (client.initialize() == QC_STATUS_OK) {
 *     client.registerVertex(samples);
 *     client.start();
 *     // ... application runs ...
 *     client.stop();
 * }
 * @endcode
 */
class HeteroSchedulerWrapper
{
public:
    /**
     * @brief Vertex execution callback type.
     *
     * The callback receives:
     * - rids: Array of resource identifiers (hardware cores)
     * - count: Number of resource identifiers
     * Returns: 0 on success, non-zero on error
     */
    using VertexCallback = std::function<void( const std::uint32_t *, std::size_t )>;

    /**
     * @brief Get the singleton instance of HeteroSchedulerWrapper.
     *
     * @return Reference to the singleton instance
     */
    static HeteroSchedulerWrapper &getInstance();

    /**
     * @brief Destructor - ensures proper cleanup.
     */
    ~HeteroSchedulerWrapper();

    // Disable copy and move operations
    HeteroSchedulerWrapper( const HeteroSchedulerWrapper & ) = delete;
    HeteroSchedulerWrapper &operator=( const HeteroSchedulerWrapper & ) = delete;
    HeteroSchedulerWrapper( HeteroSchedulerWrapper && ) = delete;
    HeteroSchedulerWrapper &operator=( HeteroSchedulerWrapper && ) = delete;

    /**
     * @brief Initialize the HeteroScheduler client.
     *
     * This performs the following steps:
     * 1. Calls cf::OrchInit() to initialize the orchestrator environment
     * 2. Loads the schedule via cf::OrchConfig()
     * 3. Obtains the schedule view via cf::OrchSchedView()
     * 4. Creates the ThreadPool for this client
     *
     * @param clientName The client identifier (must match the schedule)
     * @return QC_STATUS_OK on success, error code on failure
     */
    QCStatus_e Initialize( const std::string &clientName );

    /**
     * @brief Register vertex execution callbacks for all samples.
     *
     * This method iterates through the schedule, and for each task belonging to this client,
     * it finds the corresponding sample node by matching the task hash with the sample's name.
     * It then registers the sample's callback to be executed when the task is scheduled.
     *
     * @param samples Vector of sample nodes (SampleIF pointers)
     * @return QC_STATUS_OK on success, error code on failure
     */
    QCStatus_e RegisterVertex( const std::vector<class SampleIF *> &samples );

    /**
     * @brief Register the client and start execution.
     *
     * This performs the following steps:
     * 1. Registers the client via cf::OrchClientRegister()
     * 2. Starts the client execution via cf::OrchClientStart()
     *
     * @return QC_STATUS_OK on success, error code on failure
     */
    QCStatus_e Start();

    /**
     * @brief Stop client execution.
     *
     * This stops the worker threads and gracefully shuts down the client.
     *
     * @return QC_STATUS_OK on success, error code on failure
     */
    QCStatus_e Stop();

    /**
     * @brief Deinitialize the orchestrator.
     *
     * This calls cf::OrchDeinit() to clean up orchestrator resources.
     * Should be called after Stop() and before program termination.
     *
     * @return QC_STATUS_OK on success, error code on failure
     */
    QCStatus_e Deinit();

    /**
     * @brief Check if the client is initialized.
     *
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return m_bInitialized; }

    /**
     * @brief Check if the client is running.
     *
     * @return true if running, false otherwise
     */
    bool IsRunning() const { return m_bRunning; }

    /**
     * @brief Get the client name.
     *
     * @return The client identifier
     */
    const std::string &GetClientName() const { return m_clientName; }

private:
    /**
     * @brief Private constructor for singleton pattern.
     */
    HeteroSchedulerWrapper();

    std::string m_clientName;                       ///< Client identifier
    bool m_bInitialized;                            ///< Initialization state
    bool m_bRunning;                                ///< Running state
    std::vector<std::string> m_vertNames;           ///< Name of node vertices

    /**
     * @brief Internal cleanup helper.
     */
    void Cleanup();

    QC_DECLARE_LOGGER();
};

#endif   // QC_HETERO_SCHEDULER_WRAPPER_HPP

}   // namespace sample
}   // namespace QC