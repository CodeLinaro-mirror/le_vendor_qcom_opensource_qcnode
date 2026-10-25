// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_HPP
#define QC_LOGGER_HPP

#include "QC/Infras/Log/Ifs/QCLoggerBackendIfs.hpp"
#include "QC/Infras/Log/Ifs/QCLoggerIfs.hpp"
#include <memory>
#include <mutex>
#include <string>

namespace QC
{

class QCLoggerRegistry;

#ifndef DISABLE_QC_LOG

/** @brief A set of macros to be used by the QC components or utils.
 *
 * static void NonClassAPI(void) {
 *    QC_LOG_DEBUG( "NonClassAPI called" );
 * }
 *
 * class ComponentA {
 *   QCStatus_e Init( const char *pName, const char *pName, Logger_Level_e level ) {
 *     QCStatus_e ret = QC_LOGGER_INIT(); # do logger Init at the begin
 *     ... # those the QC_VERBOSE|DEBUG|INFO|WARN|ERROR related macros can be used.
 *     QC_INFO( "componentA: Init" );
 *  }
 *
 *  QCStatus_e Deinit( ) {
 *     QCStatus_e ret;
 *     ...
 *     QC_INFO( "componentA: Deinit" );
 *     ret = QC_LOGGER_DEINIT(); # Do logger Deinit at the end
 *  }
 *
 *  private:
 *    QC_DECLARE_LOGGER();
 * }
 */

/** @brief Define a logger that to be used by a class. */
#define QC_DECLARE_LOGGER() Logger m_logger

/** @brief Do initialization of the logger. */
#define QC_LOGGER_INIT( pName, level ) m_logger.Init( ( pName ), ( level ) )

/** @brief Do deinitialization of the logger. */
#define QC_LOGGER_DEINIT() m_logger.Deinit()


/**
 * @brief Do log a message
 * @param[in] logger the logger object use to do logger
 * @param[in] level the message log level
 * @param[in] pFormat the message format
 * @param[in] args variable arguments
 * @return void
 * @detdesc
 * Do log a message by calling the API Log of the logger.
 */
#define QC_LOGGER_LOG( logger, level, format, ... )                                                \
    ( logger ).Log( ( level ), "%s:%d " format, __FILE__, __LINE__, ##__VA_ARGS__ )


/** @brief Do log a verbose level message within a class API.
 * Note: the class must has a logger member by using QC_DECLARE_LOGGER, the same for
 * QC_DEBUG|INFO|WARN|ERROR. */
#define QC_VERBOSE( format, ... )                                                                  \
    QC_LOGGER_LOG( m_logger, LOGGER_LEVEL_VERBOSE, "VERBOSE: " format, ##__VA_ARGS__ )

/** @brief Do log a debug level message within a class API. */
#define QC_DEBUG( format, ... )                                                                    \
    QC_LOGGER_LOG( m_logger, LOGGER_LEVEL_DEBUG, "DEBUG: " format, ##__VA_ARGS__ )

/** @brief Do log a information level message within a class API. */
#define QC_INFO( format, ... )                                                                     \
    QC_LOGGER_LOG( m_logger, LOGGER_LEVEL_INFO, "INFO: " format, ##__VA_ARGS__ )

/** @brief Do log a warning level message within a class API. */
#define QC_WARN( format, ... )                                                                     \
    QC_LOGGER_LOG( m_logger, LOGGER_LEVEL_WARN, "WARN: " format, ##__VA_ARGS__ )

/** @brief Do log a error level message within a class API. */
#define QC_ERROR( format, ... )                                                                    \
    QC_LOGGER_LOG( m_logger, LOGGER_LEVEL_ERROR, "ERROR: " format, ##__VA_ARGS__ )


/** @brief Do log a verbose level message in non-class API. */
#define QC_LOG_VERBOSE( format, ... )                                                              \
    QC_LOGGER_LOG( Logger::GetDefault(), LOGGER_LEVEL_VERBOSE, "VERBOSE: " format, ##__VA_ARGS__ )

/** @brief Do log a debug level message in non-class API. */
#define QC_LOG_DEBUG( format, ... )                                                                \
    QC_LOGGER_LOG( Logger::GetDefault(), LOGGER_LEVEL_DEBUG, "DEBUG: " format, ##__VA_ARGS__ )

/** @brief Do log a information level message in non-class API. */
#define QC_LOG_INFO( format, ... )                                                                 \
    QC_LOGGER_LOG( Logger::GetDefault(), LOGGER_LEVEL_INFO, "INFO: " format, ##__VA_ARGS__ )

/** @brief Do log a warning level message in non-class API. */
#define QC_LOG_WARN( format, ... )                                                                 \
    QC_LOGGER_LOG( Logger::GetDefault(), LOGGER_LEVEL_WARN, "WARN: " format, ##__VA_ARGS__ )

/** @brief Do log a error level message in non-class API. */
#define QC_LOG_ERROR( format, ... )                                                                \
    QC_LOGGER_LOG( Logger::GetDefault(), LOGGER_LEVEL_ERROR, "ERROR: " format, ##__VA_ARGS__ )
#else
#define QC_DECLARE_LOGGER()

#define QC_LOGGER_INIT( pName, level ) QC_STATUS_OK
#define QC_LOGGER_DEINIT() QC_STATUS_OK

#define QC_LOGGER_LOG( logger, level, format, ... )

#define QC_VERBOSE( format, ... )
#define QC_DEBUG( format, ... )
#define QC_INFO( format, ... )
#define QC_WARN( format, ... )
#define QC_ERROR( format, ... )

#define QC_LOG_VERBOSE( format, ... )
#define QC_LOG_DEBUG( format, ... )
#define QC_LOG_INFO( format, ... )
#define QC_LOG_WARN( format, ... )
#define QC_LOG_ERROR( format, ... )
#endif

/** @brief qcnode::Logger */
class Logger : public QCLoggerIfs
{
public:
    /** @brief Construct an uninitialized Logger. */
    Logger() noexcept;
    Logger( const Logger & ) = delete;
    Logger &operator=( const Logger & ) = delete;

    /** @brief Destroy the Logger and release its active backend. */
    ~Logger() override;

    /**
     * @brief Initialize the logger.
     * @param[in] pName The name of the logger.
     * @param[in] level The message log level.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, others on failure.
     */
    QCStatus_e Init( const char *pName, Logger_Level_e level = LOGGER_LEVEL_ERROR,
                     LoggerBackend_e backend = LOGGER_BACKEND_DEFAULT ) override;

    /**
     * @brief Deinitialize the logger.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e Deinit() override;

    /**
     * @brief Log a message with variable arguments.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] ... The variable arguments for the format string.
     * @return void
     */
    void Log( Logger_Level_e level, const char *pFormat, ... ) const override;

    /**
     * @brief Log a message with a variable-argument list.
     * @param[in] level The message log level.
     * @param[in] pFormat The message format string.
     * @param[in] args The variable arguments for the format string.
     * @return void
     */
    void Log( Logger_Level_e level, const char *pFormat, va_list args ) const override;

    /**
     * @brief Get the actual log level of this logger.
     * @return The current Logger message level.
     */
    Logger_Level_e GetLevel() const override { return m_level; }

    /**
     * @brief Set the message log level of this logger.
     * @param[in] level The message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SetLevel( Logger_Level_e level ) override;

    /**
     * @brief Switch the output backend while retaining the logger name and level.
     * @param[in] backend The backend used to output log messages.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    QCStatus_e SwitchBackend( LoggerBackend_e backend ) override;

    /**
     * @brief Get the name used to register this logger.
     * @return The Logger name.
     */
    const char *GetName() const { return m_name.c_str(); }

    /**
     * @brief Get the default QC logger.
     * @return The default Logger instance.
     */
    static Logger &GetDefault();

    /**
     * @brief Set the level used when the default logger is first initialized.
     * @param[in] level The default message log level.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    static QCStatus_e SetDefaultLevel( Logger_Level_e level );

    /**
     * @brief Set the backend used when the default logger is first initialized.
     * @param[in] backend The default output backend.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    static QCStatus_e SetDefaultBackend( LoggerBackend_e backend );

private:
    friend class QCLoggerRegistry;

    void SetNameForRegistry( const std::string &name ) { m_name = name; }
    Logger_Level_e DecideLoggerLevel( const char *pName, Logger_Level_e level ) const;

    std::unique_ptr<QCLoggerBackendIfs> m_pBackend;
    Logger_Level_e m_level = LOGGER_LEVEL_ERROR;
    std::string m_name;
    static LoggerBackend_e s_defaultBackend;
    static Logger_Level_e s_defaultLevel;
    static Logger s_defaultLogger;
    static std::mutex s_defaultLoggerLock;
};

}   // namespace QC

#endif   // QC_LOGGER_HPP
