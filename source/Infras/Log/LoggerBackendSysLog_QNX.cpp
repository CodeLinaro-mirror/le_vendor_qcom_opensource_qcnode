// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#if defined( __QNXNTO__ )
#include "LoggerBackendSysLog.hpp"
#include <stdio.h>
#include <sys/slog2.h>

namespace QC
{

extern "C" char *__progname;

static bool IsValidLevelForSlog2( Logger_Level_e level )
{
    return ( level >= LOGGER_LEVEL_VERBOSE ) && ( level < LOGGER_LEVEL_MAX );
}

void LoggerBackendSysLog::Log( Logger_Level_e level, const char *pFormat, va_list args ) const
{
    slog2_buffer_t hBuffer = static_cast<slog2_buffer_t>( m_hHandle );
    va_list slogArgs;
    int rc;
    if ( ( nullptr != hBuffer ) && ( nullptr != pFormat ) && IsValidLevelForSlog2( level ) )
    {
        // Always use SLOG2_INFO because DEBUG and VERBOSE messages may be dropped.
        va_copy( slogArgs, args );
        rc = vslog2f( hBuffer, 0, SLOG2_INFO, pFormat, slogArgs );
        va_end( slogArgs );
        if ( 0 != rc )
        {
            (void) fprintf( stderr, "slog2f error=%d: ", rc );
            (void) vfprintf( stderr, pFormat, args );
        }
    }
}

QCStatus_e LoggerBackendSysLog::Init( const char *pName, Logger_Level_e level )
{
    QCStatus_e status = QC_STATUS_OK;
    slog2_buffer_t hBuffer = nullptr;
    slog2_buffer_set_config_t bufferConfig{};
    (void) level;

    if ( nullptr == pName )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        status = m_name.Assign( pName );
        if ( QC_STATUS_OK == status )
        {
            bufferConfig.num_buffers = 1;
            bufferConfig.verbosity_level = SLOG2_DEBUG2;
            bufferConfig.buffer_config[0].buffer_name = m_name.CStr();
            bufferConfig.buffer_set_name = __progname;
            bufferConfig.buffer_config[0].num_pages = 32;

            const int result =
                    slog2_register( &bufferConfig, &hBuffer, SLOG2_TRY_REUSE_BUFFER_SET );
            if ( ( 0 == result ) && ( nullptr != hBuffer ) )
            {
                m_hHandle = static_cast<Logger_Handle_t>( hBuffer );
            }
            else
            {
                (void) fprintf( stderr, "ERROR: failed to create slog2 %s: %d\n",
                                m_name.CStr(), result );
                status = QC_STATUS_FAIL;
            }
        }
    }
    return status;
}

QCStatus_e LoggerBackendSysLog::Deinit()
{
    m_hHandle = nullptr;
    return m_name.Clear();
}

}   // namespace QC

#endif /* __QNXNTO__ */
