// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_RADAR_ABSTRACT_IFACE_HPP
#define QC_RADAR_ABSTRACT_IFACE_HPP

#include <stddef.h>
#include <stdint.h>

namespace q
{
namespace interface
{

static constexpr int RADAR_OK = 0;
static constexpr int RADAR_ETIMEOUT = -1;
static constexpr int RADAR_EINVAL = -2;
static constexpr int RADAR_EPROTO = -3;

class IRadar
{
public:
    IRadar() = default;
    virtual ~IRadar() = default;
    IRadar( const IRadar & ) = delete;
    IRadar &operator=( const IRadar & ) = delete;
    IRadar( IRadar && ) = delete;
    IRadar &operator=( IRadar && ) = delete;
    virtual bool IsOpen() const = 0;
    virtual int Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                         size_t outputSize ) = 0;
};

}   // namespace interface
}   // namespace q

#endif   // QC_RADAR_ABSTRACT_IFACE_HPP
