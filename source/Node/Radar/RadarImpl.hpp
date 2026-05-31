// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_RADAR_IMPL_HPP
#define QC_NODE_RADAR_IMPL_HPP

#include "QC/Node/Ifs/QCNodeDefs.hpp"

#include "IRadarIf.hpp"

#include <memory>

namespace QC
{
namespace Node
{

class RadarImpl
{
public:
    RadarImpl();
    RadarImpl( const RadarImpl & ) = delete;
    RadarImpl &operator=( const RadarImpl & ) = delete;
    RadarImpl( RadarImpl && ) = delete;
    RadarImpl &operator=( RadarImpl && ) = delete;
    QCStatus_e Initialize( const char *devicePath, uint32_t timeoutMs = 5000 );
    QCStatus_e Deinitialize();
    QCStatus_e Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                        size_t outputSize );
    bool IsInitialized() const;

private:
    std::unique_ptr<q::interface::IRadar> m_radar;
    bool m_initialized;
    uint32_t m_timeoutMs;

#ifdef QC_RADAR_IMPL_FRIEND_CLASS_UT
    QC_RADAR_IMPL_FRIEND_CLASS_UT
#endif

#ifdef QC_RADAR_IFACE_FRIEND_CLASS_UT
    QC_RADAR_IFACE_FRIEND_CLASS_UT
#endif
};

}   // namespace Node
}   // namespace QC

#endif   // QC_NODE_RADAR_IMPL_HPP
