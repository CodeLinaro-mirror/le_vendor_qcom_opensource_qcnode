// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_BACKEND_SYSLOG_HPP
#define QC_LOGGER_BACKEND_SYSLOG_HPP

#include "QC/Common/QCString.hpp"
#include "QC/Infras/Log/Ifs/QCLoggerBackendIfs.hpp"

namespace QC
{

class LoggerBackendSysLog final : public QCLoggerBackendIfs
{
public:
    /**
     * @brief Initialize the SysLog Logger backend.
     * @param[in] pName The Logger name.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Init( const char *pName, Logger_Level_e level ) override;
    /**
     * @brief Deinitialize the SysLog Logger backend.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Deinit() override;
    /**
     * @brief Output a message to the system log.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] args The variable arguments for the format string.
     * @return void
     */
    void Log( Logger_Level_e level, const char *pFormat, va_list args ) const override;

private:
    QCString<LOGGER_NAME_MAX> m_name;
    Logger_Handle_t m_hHandle = nullptr;
};

}   // namespace QC

#endif   // QC_LOGGER_BACKEND_SYSLOG_HPP
