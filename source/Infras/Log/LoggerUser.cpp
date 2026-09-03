// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Log/LoggerUser.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

Logger_Create_t LoggerUser::s_createFnc = &LoggerUser::DummyCreate;
Logger_Log_t LoggerUser::s_logFnc = &LoggerUser::DummyLog;
Logger_Destroy_t LoggerUser::s_destroyFnc = &LoggerUser::DummyDestroy;
bool LoggerUser::s_isSetup = false;

QCStatus_e LoggerUser::Init( const char *pName, Logger_Level_e level )
{
    m_hHandle = nullptr;
    return s_createFnc( pName, level, &m_hHandle );
}

QCStatus_e LoggerUser::Deinit()
{
    s_destroyFnc( m_hHandle );
    m_hHandle = nullptr;
    return QC_STATUS_OK;
}

void LoggerUser::Log( Logger_Level_e level, const char *pFormat, va_list args ) const
{
    s_logFnc( m_hHandle, level, pFormat, args );
}

QCStatus_e LoggerUser::Setup( Logger_Log_t logFnc, Logger_Create_t createFnc,
                              Logger_Destroy_t destroyFnc )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( ( nullptr == logFnc ) || ( nullptr == createFnc ) || ( nullptr == destroyFnc ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( true == s_isSetup )
    {
        if ( ( s_logFnc != logFnc ) || ( s_createFnc != createFnc ) ||
             ( s_destroyFnc != destroyFnc ) )
        {
            status = QC_STATUS_BAD_STATE;
        }
    }
    else
    {
        s_logFnc = logFnc;
        s_createFnc = createFnc;
        s_destroyFnc = destroyFnc;
        s_isSetup = true;
    }

    if ( QC_STATUS_OK == status )
    {
        (void) Logger::SetDefaultBackend( LOGGER_BACKEND_USER );
        (void) Logger::GetDefault();
    }
    return status;
}

QCStatus_e LoggerUser::DummyCreate( const char *, Logger_Level_e, Logger_Handle_t *pHandle )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == pHandle )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        *pHandle = nullptr;
    }
    return status;
}

void LoggerUser::DummyLog( Logger_Handle_t, Logger_Level_e, const char *, va_list ) {}

void LoggerUser::DummyDestroy( Logger_Handle_t ) {}

}   // namespace QC
