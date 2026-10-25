// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "LoggerBackendStdout.hpp"
#include <cstring>
#include <stdio.h>

namespace QC
{

QCStatus_e LoggerBackendStdout::Init( const char *pName, Logger_Level_e )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == pName )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        m_name = pName;
    }
    return status;
}

QCStatus_e LoggerBackendStdout::Deinit()
{
    m_name.clear();
    return QC_STATUS_OK;
}

void LoggerBackendStdout::Log( Logger_Level_e, const char *pFormat, va_list args ) const
{
    if ( nullptr != pFormat )
    {
        (void) printf( "%s: ", m_name.c_str() );
        (void) vprintf( pFormat, args );
        const size_t length = std::strlen( pFormat );
        if ( ( 0U == length ) || ( '\n' != pFormat[length - 1U] ) )
        {
            (void) printf( "\n" );
        }
    }
}

}   // namespace QC
