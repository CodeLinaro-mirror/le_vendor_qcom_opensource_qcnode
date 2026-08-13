// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#pragma once
#include "IRadarIf.hpp"

namespace q
{
namespace interface
{

class RadarStub : public IRadar
{
public:
    bool m_isOpen        = true;
    int  m_executeResult = RADAR_OK;

    bool IsOpen() const override { return m_isOpen; }

    int Execute( uint64_t, size_t, uint64_t, size_t ) override { return m_executeResult; }

    ~RadarStub() override = default;
};

}   // namespace interface
}   // namespace q
