// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_BACKEND_STDOUT_HPP
#define QC_LOGGER_BACKEND_STDOUT_HPP

#include "QC/Infras/Log/Ifs/QCLoggerBackendIfs.hpp"
#include <string>

namespace QC
{

class LoggerBackendStdout final : public QCLoggerBackendIfs
{
public:
    /**
     * @brief Initialize the stdout Logger backend.
     * @param[in] pName The Logger name.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Init( const char *pName, Logger_Level_e level ) override;
    /**
     * @brief Deinitialize the stdout Logger backend.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Deinit() override;
    /**
     * @brief Output a message to stdout.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] args The variable arguments for the format string.
     * @return void
     */
    void Log( Logger_Level_e level, const char *pFormat, va_list args ) const override;

private:
    std::string m_name;
};

}   // namespace QC

#endif   // QC_LOGGER_BACKEND_STDOUT_HPP
