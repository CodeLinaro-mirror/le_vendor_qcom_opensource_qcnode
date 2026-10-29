// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Log/Logger.hpp"
#include "LoggerBackendStdout.hpp"
#include "LoggerBackendSysLog.hpp"
#include "QC/Infras/Log/LoggerUser.hpp"
#include "QC/Infras/Log/QCLoggerRegistry.hpp"
#include <cstring>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

namespace QC
{
namespace
{
bool IsValidLevel( Logger_Level_e level )
{
    return ( level >= LOGGER_LEVEL_VERBOSE ) && ( level < LOGGER_LEVEL_MAX );
}

bool IsValidBackend( LoggerBackend_e backend )
{
    return ( backend >= LOGGER_BACKEND_DEFAULT ) && ( backend < LOGGER_BACKEND_MAX );
}

std::unique_ptr<QCLoggerBackendIfs> CreateBackend( LoggerBackend_e backend )
{
    QCLoggerBackendIfs *pBackend = nullptr;
    switch ( backend )
    {
        case LOGGER_BACKEND_SYSLOG:
            pBackend = new ( std::nothrow ) LoggerBackendSysLog();
            break;
        case LOGGER_BACKEND_STDOUT:
            pBackend = new ( std::nothrow ) LoggerBackendStdout();
            break;
        case LOGGER_BACKEND_USER:
            pBackend = new ( std::nothrow ) LoggerUser();
            break;
        default:
            break;
    }
    return std::unique_ptr<QCLoggerBackendIfs>( pBackend );
}
}   // namespace

LoggerBackend_e Logger::s_defaultBackend = LOGGER_BACKEND_SYSLOG;
Logger_Level_e Logger::s_defaultLevel = LOGGER_LEVEL_ERROR;
Logger Logger::s_defaultLogger;
std::mutex Logger::s_defaultLoggerLock;

Logger::Logger() noexcept = default;
Logger::~Logger()
{
    if ( nullptr != m_pBackend )
    {
        (void) Deinit();
    }
}

QCStatus_e Logger::Init( const char *pName, Logger_Level_e level, LoggerBackend_e backend )
{
    QCStatus_e status = QC_STATUS_OK;
    std::unique_ptr<QCLoggerBackendIfs> pBackend;
    bool bNameTooLong = false;
    const bool bValidLevel = IsValidLevel( level );
    const bool bValidBackend = IsValidBackend( backend );

    if ( nullptr != pName )
    {
        bNameTooLong = std::strlen( pName ) > LOGGER_NAME_MAX;
    }

    if ( ( nullptr == pName ) || ( '\0' == pName[0] ) || ( true == bNameTooLong ) ||
         ( false == bValidLevel ) || ( false == bValidBackend ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( nullptr != m_pBackend )
    {
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        if ( LOGGER_BACKEND_DEFAULT == backend )
        {
            backend = s_defaultBackend;
        }
        pBackend = CreateBackend( backend );
        if ( nullptr == pBackend )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            m_name = pName;
            m_pBackend = std::move( pBackend );
            status = QCLoggerRegistry::Instance().Register( this );
            if ( QC_STATUS_OK == status )
            {
                m_level = DecideLoggerLevel( m_name.c_str(), level );
                status = m_pBackend->Init( m_name.c_str(), m_level );
                if ( QC_STATUS_OK != status )
                {
                    (void) QCLoggerRegistry::Instance().Unregister( this );
                    m_pBackend.reset();
                    m_level = LOGGER_LEVEL_ERROR;
                    m_name.clear();
                }
            }
            else
            {
                m_pBackend.reset();
                m_level = LOGGER_LEVEL_ERROR;
                m_name.clear();
            }
        }
    }
    return status;
}

QCStatus_e Logger::Deinit()
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == m_pBackend )
    {
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        (void) QCLoggerRegistry::Instance().Unregister( this );
        status = m_pBackend->Deinit();
        m_pBackend.reset();
        m_level = LOGGER_LEVEL_ERROR;
        m_name.clear();
    }
    return status;
}

void Logger::Log( Logger_Level_e level, const char *pFormat, ... ) const
{
    va_list args;
    va_start( args, pFormat );
    Log( level, pFormat, args );
    va_end( args );
}

void Logger::Log( Logger_Level_e level, const char *pFormat, va_list args ) const
{
    if ( ( nullptr != m_pBackend ) && ( nullptr != pFormat ) && IsValidLevel( level ) &&
         ( level >= m_level ) )
    {
        m_pBackend->Log( level, pFormat, args );
    }
}

QCStatus_e Logger::SetLevel( Logger_Level_e level )
{
    QCStatus_e status = QC_STATUS_OK;
    const bool bValidLevel = IsValidLevel( level );
    if ( false == bValidLevel )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        m_level = level;
    }
    return status;
}

QCStatus_e Logger::SwitchBackend( LoggerBackend_e backend )
{
    QCStatus_e status = QC_STATUS_OK;
    std::unique_ptr<QCLoggerBackendIfs> pBackend;
    const bool bValidBackend = IsValidBackend( backend );

    if ( false == bValidBackend )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else if ( nullptr == m_pBackend )
    {
        status = QC_STATUS_BAD_STATE;
    }
    else
    {
        if ( LOGGER_BACKEND_DEFAULT == backend )
        {
            backend = s_defaultBackend;
        }
        pBackend = CreateBackend( backend );
        if ( nullptr == pBackend )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            status = pBackend->Init( m_name.c_str(), m_level );
            if ( QC_STATUS_OK == status )
            {
                (void) m_pBackend->Deinit();
                m_pBackend = std::move( pBackend );
            }
        }
    }
    return status;
}

Logger &Logger::GetDefault()
{
    if ( nullptr == s_defaultLogger.m_pBackend )
    {
        std::lock_guard<std::mutex> lock( s_defaultLoggerLock );
        (void) s_defaultLogger.Init( "QCNODE", s_defaultLevel, s_defaultBackend );
    }
    return s_defaultLogger;
}

QCStatus_e Logger::SetDefaultLevel( Logger_Level_e level )
{
    QCStatus_e status = QC_STATUS_OK;
    const bool bValidLevel = IsValidLevel( level );
    if ( false == bValidLevel )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        s_defaultLevel = level;
    }
    return status;
}

QCStatus_e Logger::SetDefaultBackend( LoggerBackend_e backend )
{
    QCStatus_e status = QC_STATUS_OK;
    const bool bValidBackend = IsValidBackend( backend );
    if ( ( backend <= LOGGER_BACKEND_DEFAULT ) || ( false == bValidBackend ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        s_defaultBackend = backend;
    }
    return status;
}

Logger_Level_e Logger::DecideLoggerLevel( const char *pName, Logger_Level_e level ) const
{
    Logger_Level_e loggerLevel = level;
    const std::string nameEnv = std::string( pName ) + "_QC_LOG_LEVEL";
    const char *pEnvName = nameEnv.c_str();
    const char *pEnvValue = getenv( pEnvName );
    if ( nullptr == pEnvValue )
    {
        pEnvName = "QC_LOG_LEVEL";
        pEnvValue = getenv( pEnvName );
    }
    if ( nullptr != pEnvValue )
    {
        const std::string value( pEnvValue );
        if ( "VERBOSE" == value )
        {
            loggerLevel = LOGGER_LEVEL_VERBOSE;
        }
        else if ( "DEBUG" == value )
        {
            loggerLevel = LOGGER_LEVEL_DEBUG;
        }
        else if ( "INFO" == value )
        {
            loggerLevel = LOGGER_LEVEL_INFO;
        }
        else if ( "WARN" == value )
        {
            loggerLevel = LOGGER_LEVEL_WARN;
        }
        else if ( "ERROR" == value )
        {
            loggerLevel = LOGGER_LEVEL_ERROR;
        }
        else
        {
            (void) fprintf( stderr, "ERROR: invalid env %s=%s\n", pEnvName, pEnvValue );
        }
    }
    return loggerLevel;
}

}   // namespace QC
