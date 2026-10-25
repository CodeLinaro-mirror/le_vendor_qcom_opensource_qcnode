// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_DEFS_HPP
#define QC_LOGGER_DEFS_HPP

#include "QC/Common/Types.hpp"
#include <stdarg.h>

namespace QC
{

/** @brief The message log level */
typedef enum
{
    LOGGER_LEVEL_VERBOSE, /**< The level for the verbose message */
    LOGGER_LEVEL_DEBUG,   /**< The level for the debug message */
    LOGGER_LEVEL_INFO,    /**< The level for the information message */
    LOGGER_LEVEL_WARN,    /**< The level for the warning message */
    LOGGER_LEVEL_ERROR,   /**< The level for the error message */
    LOGGER_LEVEL_MAX
} Logger_Level_e;

/** @brief The backend used to output log messages. */
typedef enum
{
    LOGGER_BACKEND_DEFAULT,
    LOGGER_BACKEND_SYSLOG,
    LOGGER_BACKEND_STDOUT,
    LOGGER_BACKEND_USER,
    LOGGER_BACKEND_MAX
} LoggerBackend_e;

/** @brief The maximum Logger name length, excluding the terminating null. */
static constexpr uint32_t LOGGER_NAME_MAX = 127U;

/** @brief The handle to the log system  */
typedef void *Logger_Handle_t;

/**
 * @brief A callback to log a message
 * @param[in] hHandle the customer used log system handle
 * @param[in] level the message log level
 * @param[in] pFormat the message format
 * @param[in] args variable arguments
 * @return void
 */
typedef void ( *Logger_Log_t )( Logger_Handle_t hHandle, Logger_Level_e level, const char *pFormat,
                                va_list args );

/**
 * @brief A callback to create the customer used log system handle
 * @param[in] pName the name of the logger
 * @param[in] level the message log level
 * @param[out] pHandle [out] the pointer to return the created customer used log system handle
 * @return QC_STATUS_OK on success, others on failure
 */
typedef QCStatus_e ( *Logger_Create_t )( const char *pName, Logger_Level_e level,
                                         Logger_Handle_t *pHandle );

/**
 * @brief A callback to destroy the customer used log system handle
 * @param[in] hHandle the customer used log system handle
 * @return void
 */
typedef void ( *Logger_Destroy_t )( Logger_Handle_t hHandle );

}   // namespace QC

#endif   // QC_LOGGER_DEFS_HPP
