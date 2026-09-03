// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "LoggerBackendSysLog.hpp"
#include <array>
#include <stdio.h>
#include <syslog.h>

namespace QC
{

static constexpr uint32_t LOGGER_FORMAT_MAX = 1024U;
static constexpr std::array<int, LOGGER_LEVEL_MAX> s_qcLoggerLevelToJournalPriority = {
        LOG_DEBUG,   /* LOGGER_LEVEL_VERBOSE */
        LOG_INFO,    /* LOGGER_LEVEL_DEBUG */
        LOG_NOTICE,  /* LOGGER_LEVEL_INFO */
        LOG_WARNING, /* LOGGER_LEVEL_WARN */
        LOG_ERR      /* LOGGER_LEVEL_ERROR */
};
static_assert( s_qcLoggerLevelToJournalPriority.size() == LOGGER_LEVEL_MAX,
               "Logger level to syslog priority mapping is incomplete" );

static bool IsValidLevelForSysLog( Logger_Level_e level )
{
    return ( level >= LOGGER_LEVEL_VERBOSE ) && ( level < LOGGER_LEVEL_MAX );
}

void LoggerBackendSysLog::Log( Logger_Level_e level, const char *pFormat, va_list args ) const
{
    QCStatus_e status = QC_STATUS_OK;
    QCString<LOGGER_FORMAT_MAX> strFmt;
    if ( ( nullptr != pFormat ) && IsValidLevelForSysLog( level ) )
    {
        status = strFmt.Assign( m_name.CStr() );
        if ( QC_STATUS_OK == status )
        {
            status = strFmt.Append( " : " );
        }
        if ( QC_STATUS_OK == status )
        {
            status = strFmt.Append( pFormat );
        }
        if ( QC_STATUS_OK == status )
        {
            const int priority =
                    s_qcLoggerLevelToJournalPriority[static_cast<size_t>( level )];
            vsyslog( priority, strFmt.CStr(), args );
        }
        else if ( QC_STATUS_NOMEM == status )
        {
            (void) fprintf( stderr, "logger buffer overflow error: %s ", m_name.CStr() );
            (void) vfprintf( stderr, pFormat, args );
            (void) fprintf( stderr, "\n" );
        }
    }
}

QCStatus_e LoggerBackendSysLog::Init( const char *pName, Logger_Level_e level )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == pName )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        status = m_name.Assign( pName );
        (void) level;
    }
    return status;
}

QCStatus_e LoggerBackendSysLog::Deinit()
{
    m_hHandle = nullptr;
    return m_name.Clear();
}

}   // namespace QC
