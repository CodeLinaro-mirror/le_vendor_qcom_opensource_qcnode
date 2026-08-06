// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "RadarImpl.hpp"
#include "RadarIf.hxx"

namespace QC
{
namespace Node
{

RadarImpl::RadarImpl() : m_radar( nullptr ), m_initialized( false ), m_timeoutMs( 5000 ) {}

QCStatus_e RadarImpl::Initialize( const char *devicePath, uint32_t timeoutMs )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( m_initialized )
    {
        status = QC_STATUS_ALREADY;
    }

    if ( ( QC_STATUS_OK == status ) && ( nullptr == m_radar ) )
    {
        if ( devicePath == nullptr )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_radar = q::interface::CreateRadar( devicePath, timeoutMs );
        }
    }

    if ( ( QC_STATUS_OK == status ) &&
         ( ( nullptr == m_radar ) || ( false == m_radar->IsOpen() ) ) )
    {
        m_radar.reset();
        status = QC_STATUS_BAD_STATE;
    }

    if ( QC_STATUS_OK == status )
    {
        m_timeoutMs = timeoutMs;
        m_initialized = true;
    }

    return status;
}

QCStatus_e RadarImpl::Deinitialize()
{
    m_radar.reset();
    m_initialized = false;
    return QC_STATUS_OK;
}

bool RadarImpl::IsInitialized() const
{
    return m_initialized;
}

QCStatus_e RadarImpl::Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                               size_t outputSize )
{
    QCStatus_e status;

    if ( ( false == m_initialized ) || ( nullptr == m_radar ) )
    {
        status = QC_STATUS_BAD_STATE;
    }
    else if ( ( inputHandle == 0ULL ) || ( outputHandle == 0ULL ) || ( inputSize == 0U ) ||
              ( outputSize == 0U ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        int result = m_radar->Execute( inputHandle, inputSize, outputHandle, outputSize );
        if ( result == 0 )
        {
            status = QC_STATUS_OK;
        }
#ifdef __linux__
        else if ( result == q::interface::RADAR_ETIMEOUT )
        {
            status = QC_STATUS_TIMEOUT;
        }
        else if ( result == q::interface::RADAR_EINVAL )
        {
            status = QC_STATUS_INVALID_BUF;
        }
#endif
        else
        {
            status = QC_STATUS_FAIL;
        }
    }
    return status;
}

}   // namespace Node
}   // namespace QC
