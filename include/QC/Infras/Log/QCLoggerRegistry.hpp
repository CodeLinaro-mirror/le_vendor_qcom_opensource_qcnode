// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_REGISTRY_HPP
#define QC_LOGGER_REGISTRY_HPP

#include "QC/Common/QCString.hpp"
#include "QC/Infras/Log/Ifs/QCLoggerDefs.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace QC
{

class Logger;

class QCLoggerRegistry
{
public:
    using LoggerName_t = QCString<LOGGER_NAME_MAX>;

    /**
     * @brief Get the Logger registry singleton.
     * @return The Logger registry singleton.
     */
    static QCLoggerRegistry &Instance();

    /**
     * @brief Get the number of registered Loggers.
     * @param[out] count The number of registered Loggers.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e GetNumber( uint32_t &count ) const;

    /**
     * @brief Get the names of registered Loggers.
     * @param[out] pNameArray The caller-allocated array receiving Logger names.
     * @param[in] count The number of elements in pNameArray. Call GetNumber() first to obtain
     *                   the required count.
     * @return QC_STATUS_OK on success, QC_STATUS_NO_RESOURCE when count is smaller than the
     *         current number of registered Loggers, otherwise an error status.
     */
    QCStatus_e GetLogger( LoggerName_t *pNameArray, uint32_t count ) const;

    /**
     * @brief Set a Logger message level by name.
     * @param[in] pName The Logger name.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SetLogLevel( const char *pName, Logger_Level_e level );

    /**
     * @brief Set a Logger output backend by name.
     * @param[in] pName The Logger name.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SetLogBackend( const char *pName, LoggerBackend_e backend );

    /**
     * @brief Set the message level of all registered Loggers.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SetLogLevel( Logger_Level_e level );

    /**
     * @brief Set the output backend of all registered Loggers.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SetLogBackend( LoggerBackend_e backend );

    /**
     * @brief Register a Logger for runtime control.
     * Duplicate names are disambiguated by appending an increasing suffix.
     * @param[in] pLogger The Logger to register.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Register( Logger *pLogger );

    /**
     * @brief Remove a Logger from runtime control.
     * @param[in] pLogger The Logger to remove.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Unregister( Logger *pLogger );

private:
    QCLoggerRegistry() = default;
    Logger *Find( const char *pName ) const;

    mutable std::mutex m_lock;
    std::unordered_map<std::string, Logger *> m_loggers;
    std::unordered_map<std::string, uint32_t> m_nameIndices;
};

}   // namespace QC

#endif   // QC_LOGGER_REGISTRY_HPP
