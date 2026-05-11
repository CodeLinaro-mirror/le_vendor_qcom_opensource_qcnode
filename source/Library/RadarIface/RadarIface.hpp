// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_RADAR_IFACE_HPP
#define QC_RADAR_IFACE_HPP

#include "QC/Common/Types.hpp"

namespace QC
{
namespace Library
{

/**
 * @brief QC wrapper for the radar interface
 * This class wraps the q::interface::Radar to provide QC-style interface
 */
class RadarIface
{
public:
    RadarIface();
    ~RadarIface();

    /**
     * @brief Initialize the radar interface
     * @param[in] devicePath Path to the radar device
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Initialize( const char *devicePath, uint32_t timeoutMs = 5000 );

    /**
     * @brief Deinitialize the radar interface
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Deinitialize();

    /**
     * @brief Execute radar processing using DMA-BUF file descriptors
     * @param[in] inputFd    DMA-BUF fd for input buffer
     * @param[in] inputSize  Input buffer size in bytes
     * @param[in] outputFd   DMA-BUF fd for output buffer
     * @param[in] outputSize Output buffer size in bytes
     * @return QC_STATUS_OK on success, others on failure
     */
    QCStatus_e Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                        size_t outputSize );

    /**
     * @brief Check if radar interface is initialized
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const;

private:
    class Impl;
    Impl *m_pImpl;
};

}   // namespace Library
}   // namespace QC

#endif   // QC_RADAR_IFACE_HPP
