// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "RadarIface.hpp"
#include "RadarIf.hxx"
#include <errno.h>

namespace QC
{
namespace Library
{

/**
 * @brief Implementation class for RadarIface using PIMPL pattern
 */
class RadarIface::Impl
{
public:
    Impl() : m_radar( nullptr ), m_initialized( false ), m_timeoutMs( 5000 ) {}

    ~Impl() { Deinitialize(); }

    QCStatus_e Initialize( const char *devicePath, uint32_t timeoutMs )
    {
        QCStatus_e status = QC_STATUS_OK;

        // Only initialize once
        if ( m_initialized )
        {
            status = QC_STATUS_OK;
        }
        else if ( devicePath == nullptr )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            try
            {
                m_radar = new q::interface::Radar( devicePath, timeoutMs );
                if ( m_radar == nullptr )
                {
                    status = QC_STATUS_BAD_STATE;
                }
                else
                {
                    if ( !m_radar->IsOpen() )
                    {
                        delete m_radar;
                        m_radar = nullptr;
                        status = QC_STATUS_BAD_STATE;
                    }
                    else
                    {
                        m_timeoutMs = timeoutMs;
                        m_initialized = true;
                        status = QC_STATUS_OK;
                    }
                }
            }
            catch ( ... )
            {
                status = QC_STATUS_BAD_STATE;
            }
        }
        return status;
    }

    QCStatus_e Deinitialize()
    {
        if ( m_radar && m_initialized )
        {
            delete m_radar;
            m_radar = nullptr;
            m_initialized = false;
        }
        return QC_STATUS_OK;
    }

    bool IsInitialized() const { return m_initialized; }

    QCStatus_e Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                        size_t outputSize )
    {
        QCStatus_e status = QC_STATUS_OK;
        if ( !m_initialized || !m_radar )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else if ( inputHandle == 0 || outputHandle == 0 || inputSize == 0 || outputSize == 0 )
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

private:
    q::interface::Radar *m_radar;
    bool m_initialized;
    uint32_t m_timeoutMs;
};

// RadarIface implementation
RadarIface::RadarIface() : m_pImpl( new Impl() ) {}

RadarIface::~RadarIface()
{
    delete m_pImpl;
}

QCStatus_e RadarIface::Initialize( const char *devicePath, uint32_t timeoutMs )
{
    return m_pImpl->Initialize( devicePath, timeoutMs );
}

QCStatus_e RadarIface::Deinitialize()
{
    return m_pImpl->Deinitialize();
}

bool RadarIface::IsInitialized() const
{
    return m_pImpl->IsInitialized();
}

QCStatus_e RadarIface::Execute( uint64_t inputHandle, size_t inputSize, uint64_t outputHandle,
                                size_t outputSize )
{
    return m_pImpl->Execute( inputHandle, inputSize, outputHandle, outputSize );
}

}   // namespace Library
}   // namespace QC
