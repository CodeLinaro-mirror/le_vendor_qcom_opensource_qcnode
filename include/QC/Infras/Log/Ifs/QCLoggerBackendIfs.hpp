// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_BACKEND_IFS_HPP
#define QC_LOGGER_BACKEND_IFS_HPP

#include "QC/Infras/Log/Ifs/QCLoggerDefs.hpp"

namespace QC
{

/** @brief Interface implemented by every Logger output backend. */
class QCLoggerBackendIfs
{
public:
    /** @brief Destroy the Logger backend. */
    virtual ~QCLoggerBackendIfs() = default;

    /**
     * @brief Initialize the Logger backend.
     * @param[in] pName The Logger name.
     * @param[in] level The Logger message level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e Init( const char *pName, Logger_Level_e level ) = 0;

    /**
     * @brief Deinitialize the Logger backend.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e Deinit() = 0;

    /**
     * @brief Output a Logger message.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] args The variable arguments for the format string.
     * @return void
     */
    virtual void Log( Logger_Level_e level, const char *pFormat, va_list args ) const = 0;
};

}   // namespace QC

#endif   // QC_LOGGER_BACKEND_IFS_HPP
