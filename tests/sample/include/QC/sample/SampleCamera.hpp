// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_CAMERA_HPP
#define QC_SAMPLE_CAMERA_HPP

#include "CameraBufferManager.hpp"
#include "QC/Node/Camera.hpp"
#include "QC/sample/SampleIF.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace QC
{
namespace sample
{

using namespace QC::Node;

/// @brief qcnode::sample::SampleCamera
///
/// SampleCamera demonstrates how to configure and use the QCNode Camera node.
class SampleCamera : public SampleIF
{
public:
    SampleCamera();
    ~SampleCamera();

    /// @brief Initialize the camera
    /// @param name the sample unique instance name
    /// @param config the sample config key value map
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Init( std::string name, SampleConfig_t &config );

    /// @brief Start the camera
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Start();

    /// @brief Stop the camera
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Stop();

    /// @brief Deinitialize the camera
    /// @return QC_STATUS_OK on success, others on failure
    QCStatus_e Deinit();

#ifdef QC_ENABLE_HS
    /// @brief Get the runnable callback for HeteroScheduler
    /// @return The runnable callback function
    std::function<void( const std::uint32_t *, std::size_t )> GetRunnableCallback() override;
#endif

    /**
     * @brief Retrieves the version identifier of the Node Camera.
     */
    const uint32_t GetVersion() const;

private:
    /// @brief Parse the sample config and build the camera configuration DataTree.
    QCStatus_e ParseConfig( SampleConfig_t &config );

    /// @brief Parse ISP injection parameters from the sample config and populate m_injectionCfg.
    QCStatus_e ParseInjectionConfig( SampleConfig_t &config );

    /// @brief Process a single camera frame: publish it and schedule a buffer return.
    void ProcessFrame( CameraFrameDescriptor_t *pFrameDesc );

    /// @brief Main frame-processing thread loop.
    void ThreadMain();

    /**
     * @brief Perform one-time ISP injection initialization.
     *
     * @return QC_STATUS_OK on success, an error code on missing/invalid injection
     *         configuration (e.g. zero input or output frame buffers).
     * @note EEPROM load failure is reported as an error log but does not fail the call so that
     *         injection can still proceed without static calibration data when the file
     *         is missing.
     */
    QCStatus_e InjectionInit();

    /// @brief ISP injection thread: loads raw frames and submits injection requests.
    void InjectionThreadMain();

    /// @brief Dedicated dumper thread: drains m_dumpQueue and writes each DumpData to disk.
    void DumpThreadMain();

    /**
     * @brief Update metadata in runtime for TUNING_FEATURE modes (if needed), then submit
     *        the metadata descriptor to the camera node via ProcessFrameDescriptor.
     * @param[in,out] metaDesc      The metadata descriptor to process. May be modified to
     *                              update inputCommonMetadata.bufferIdx when needsUpdate is true.
     * @param[in]     metaDataType  Metadata type of the descriptor, used to select the runtime
     *                              update branch (e.g. TUNING_FEATURE ring advance). The Camera
     *                              node does not consume the type, so it is passed here instead
     *                              of being carried on the descriptor.
     * @return QC_STATUS_OK on success, error code on failure.
     */
    QCStatus_e ProcessMetaDataDescriptor( CameraMetaDataDescriptor_t &metaDesc,
                                          CameraMetaDataType_e metaDataType );

    /// @brief Camera event callback invoked by the Camera node on frame-ready or error events.
    void ProcessDoneCb( const QCNodeEventInfo_t &eventInfo );

#ifdef QC_ENABLE_HS
    void RunnableCallback( const std::uint32_t *rids, std::size_t count );
#endif

private:
    // -------------------------------------------------------------------------
    // Camera node and configuration
    // -------------------------------------------------------------------------
    QC::Node::Camera m_camera;
    DataTree m_config;
    DataTree m_dataTree;
    QCNodeInit_t m_nodeCfg;

    std::vector<DataTree> m_streamConfigs;
    std::vector<DataTree> m_metaDataConfigs;

    // -------------------------------------------------------------------------
    // Publisher map: stream ID → topic name / publisher
    // -------------------------------------------------------------------------
    std::map<uint32_t, std::string> m_topicNameMap;
    std::map<uint32_t, std::shared_ptr<DataPublisher<DataFrames_t>>> m_pubMap;

    // -------------------------------------------------------------------------
    // Feature flags
    // -------------------------------------------------------------------------
    bool m_bEnableMetaData = false;
    bool m_bEnableInjection = false;
    bool m_bEnableFeature1Mode = false;
    bool m_bEnableFeature2Mode = false;
    bool m_bImmediateRelease = false;
    bool m_bIgnoreError = false;

    bool m_metaDataNeedsUpdate = false;
    uint32_t m_metaDataUpdateValue = 0;

    // -------------------------------------------------------------------------
    // INJECTION_SENSOR_METADATA parameters
    // -------------------------------------------------------------------------

    DataTree m_injectionCfg;

    bool m_injMultiFile = false;

    // injection input frame data buffers
    std::string m_injFileName;
    std::vector<std::string> m_injFileList;

    uint32_t m_injFrameRate = 30;
    uint32_t m_injRepeatNum = 0;
    uint32_t m_injFrameIntervalMs = 33;
    uint32_t m_injectionTotalFrames = 0;
    uint32_t m_injSensorMetaTagId = 0;

    uint32_t m_injBufSize = 0;
    uint32_t m_injInputBufNum = 0;
    uint32_t m_injFrameBufNum = 0;
    uint32_t m_injInputBufListId = 0;
    std::vector<uint32_t> m_injInputBufIds;

    // Sensor header / per-frame metadata buffers
    std::string m_injHeaderFileName;
    std::vector<std::string> m_injHeaderFileList;

    size_t m_injHeaderBufSize = 0;
    uint32_t m_injHeaderBufNum = 0;
    uint32_t m_injHeaderBufListId = 0;
    std::vector<uint32_t> m_injHeaderBufIds;

    // EEPROM calibration buffers
    std::string m_injEepromFileName;
    size_t m_injEepromBufSize = 0;
    bool m_injectionEepromLoaded = false;
    uint32_t m_injEepromBufNum = 0;
    uint32_t m_injEepromBufListId = 0;
    std::vector<uint32_t> m_injEepromBufIds;

    // Output metadata buffers
    uint32_t m_injOutputMetaBufNum = 0;
    uint32_t m_injOutputMetaBufListId = 0;
    std::vector<uint32_t> m_injOutputMetaBufIds;

    // Output frame dump
    bool m_injDumpEnable = false;
    std::string m_injDumpPath = "/tmp";


    struct DumpData
    {
        std::string fileName;
        std::vector<uint8_t> data;
    };

    static constexpr size_t kMaxDumpNum = 2;
    std::queue<DumpData> m_dumpQueue;
    std::mutex m_dumpMutex;
    std::condition_variable m_dumpCondVar;
    std::thread m_dumpThread;
    bool m_dumpStop = false;

    // ISP Injection output topic
    std::string m_injOutputTopicName;
    std::shared_ptr<DataPublisher<DataFrames_t>> m_injOutputPub;

    // Output stream
    uint32_t m_injStreamId = 0;
    std::vector<uint32_t> m_injFrameBufIds;

    // Injection thread control
    std::thread m_injectionThread;
    bool m_injectionStop = false;
    std::mutex m_injectionMutex;
    std::condition_variable m_injectionCondVar;
    uint32_t m_injFrameReadyCount = 0;

    // -------------------------------------------------------------------------
    // Main frame-processing thread state
    // -------------------------------------------------------------------------
    std::atomic<bool> m_stop{ false };
    std::mutex m_mutex;
    std::thread m_thread;
    std::condition_variable m_condVar;
    std::queue<CameraFrameDescriptor_t> m_camFrameQueue;

    // -------------------------------------------------------------------------
    // Buffer allocation and management parameters
    // -------------------------------------------------------------------------
    uint32_t m_bufferIdx = 0;

    CameraBufferManager m_camBufferManager;

    std::vector<SharedBufferPool> m_bufferPools;
    std::map<uint32_t, Profiler> m_profilers;

};   // class SampleCamera

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_CAMERA_HPP
