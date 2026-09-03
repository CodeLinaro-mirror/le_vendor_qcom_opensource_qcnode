// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Log/QCLoggerRegistry.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include <cstdlib>
#include <new>
#include <stdio.h>

namespace QC
{

QCLoggerRegistry &QCLoggerRegistry::Instance()
{
    // The default logger unregisters during static teardown. Keep the registry alive until
    // process termination so its mutex remains valid while that destructor runs.
    static QCLoggerRegistry *const pRegistry = new ( std::nothrow ) QCLoggerRegistry();
    if ( nullptr == pRegistry )
    {
        std::abort();
    }
    return *pRegistry;
}

QCStatus_e QCLoggerRegistry::Register( Logger *pLogger )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == pLogger )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        std::lock_guard<std::mutex> lock( m_lock );
        const std::string originalName( pLogger->GetName() );
        std::string registeredName = originalName;
        while ( m_loggers.end() != m_loggers.find( registeredName ) )
        {
            const uint32_t index = ++m_nameIndices[originalName];
            registeredName = originalName + "_" + std::to_string( index );
            if ( registeredName.length() > LOGGER_NAME_MAX )
            {
                status = QC_STATUS_BAD_ARGUMENTS;
                break;
            }
        }
        if ( QC_STATUS_OK == status )
        {
            if ( registeredName != originalName )
            {
                pLogger->SetNameForRegistry( registeredName );
                (void) fprintf( stderr,
                                "WARNING: duplicate logger name '%s'; registered as '%s'\n",
                                originalName.c_str(), registeredName.c_str() );
            }
            (void) m_loggers.emplace( pLogger->GetName(), pLogger );
        }
    }
    return status;
}

QCStatus_e QCLoggerRegistry::Unregister( Logger *pLogger )
{
    QCStatus_e status = QC_STATUS_OK;
    if ( nullptr == pLogger )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        std::lock_guard<std::mutex> lock( m_lock );
        const auto iterator = m_loggers.find( pLogger->GetName() );
        if ( ( m_loggers.end() == iterator ) || ( pLogger != iterator->second ) )
        {
            status = QC_STATUS_OUT_OF_BOUND;
        }
        else
        {
            m_loggers.erase( iterator );
        }
    }
    return status;
}

QCStatus_e QCLoggerRegistry::GetNumber( uint32_t &count ) const
{
    std::lock_guard<std::mutex> lock( m_lock );
    count = static_cast<uint32_t>( m_loggers.size() );
    return QC_STATUS_OK;
}

QCStatus_e QCLoggerRegistry::GetLogger( LoggerName_t *pNameArray, uint32_t count ) const
{
    QCStatus_e status = QC_STATUS_OK;
    if ( ( nullptr == pNameArray ) && ( 0U != count ) )
    {
        status = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        std::lock_guard<std::mutex> lock( m_lock );
        if ( static_cast<size_t>( count ) < m_loggers.size() )
        {
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            uint32_t index = 0U;
            for ( const auto &entry : m_loggers )
            {
                status = pNameArray[index].Assign( entry.first.c_str() );
                if ( QC_STATUS_OK != status )
                {
                    break;
                }
                ++index;
            }
        }
    }
    return status;
}

QCStatus_e QCLoggerRegistry::SetLogLevel( const char *pName, Logger_Level_e level )
{
    std::lock_guard<std::mutex> lock( m_lock );
    Logger *pLogger = Find( pName );
    return ( nullptr == pLogger ) ? QC_STATUS_OUT_OF_BOUND : pLogger->SetLevel( level );
}

QCStatus_e QCLoggerRegistry::SetLogBackend( const char *pName, LoggerBackend_e backend )
{
    std::lock_guard<std::mutex> lock( m_lock );
    Logger *pLogger = Find( pName );
    return ( nullptr == pLogger ) ? QC_STATUS_OUT_OF_BOUND : pLogger->SwitchBackend( backend );
}

QCStatus_e QCLoggerRegistry::SetLogLevel( Logger_Level_e level )
{
    QCStatus_e status = QC_STATUS_OK;
    std::lock_guard<std::mutex> lock( m_lock );
    for ( const auto &entry : m_loggers )
    {
        status = entry.second->SetLevel( level );
        if ( QC_STATUS_OK != status )
        {
            break;
        }
    }
    return status;
}

QCStatus_e QCLoggerRegistry::SetLogBackend( LoggerBackend_e backend )
{
    QCStatus_e status = QC_STATUS_OK;
    std::lock_guard<std::mutex> lock( m_lock );
    for ( const auto &entry : m_loggers )
    {
        status = entry.second->SwitchBackend( backend );
        if ( QC_STATUS_OK != status )
        {
            break;
        }
    }
    return status;
}

Logger *QCLoggerRegistry::Find( const char *pName ) const
{
    Logger *pLogger = nullptr;
    if ( nullptr != pName )
    {
        const auto iterator = m_loggers.find( pName );
        if ( m_loggers.end() != iterator )
        {
            pLogger = iterator->second;
        }
    }
    return pLogger;
}

}   // namespace QC
