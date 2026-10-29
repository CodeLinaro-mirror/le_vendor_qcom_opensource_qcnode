// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_IFS_HPP
#define QC_LOGGER_IFS_HPP

#include "QC/Infras/Log/Ifs/QCLoggerDefs.hpp"

namespace QC
{

/** @brief Interface for runtime-configurable Logger implementations. */
class QCLoggerIfs
{
public:
    /** @brief Destroy the Logger implementation. */
    virtual ~QCLoggerIfs() = default;

    /**
     * @brief Initialize the Logger.
     * @param[in] pName The Logger name.
     * @param[in] level The message log level.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e Init( const char *pName, Logger_Level_e level = LOGGER_LEVEL_ERROR,
                             LoggerBackend_e backend = LOGGER_BACKEND_DEFAULT ) = 0;

    /**
     * @brief Deinitialize the Logger.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e Deinit() = 0;

    /**
     * @brief Log a message with variable arguments.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] ... The variable arguments for the format string.
     * @return void
     */
    virtual void Log( Logger_Level_e level, const char *pFormat, ... ) const = 0;

    /**
     * @brief Log a message with a variable-argument list.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] args The variable arguments for the format string.
     * @return void
     */
    virtual void Log( Logger_Level_e level, const char *pFormat, va_list args ) const = 0;

    /**
     * @brief Get the current Logger message level.
     * @return The current Logger message level.
     */
    virtual Logger_Level_e GetLevel() const = 0;

    /**
     * @brief Set the Logger message level.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e SetLevel( Logger_Level_e level ) = 0;

    /**
     * @brief Switch the Logger output backend.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    virtual QCStatus_e SwitchBackend( LoggerBackend_e backend ) = 0;
};

}   // namespace QC

#endif   // QC_LOGGER_IFS_HPP
