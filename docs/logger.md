
*Menu*:
- [1. QC Logger Types](#1-qc-logger-types)
- [2. QC Logger Macros](#2-qc-logger-macros)
- [3. Typical Use Case](#3-typical-use-case)
  - [3.1 The QC default used log system](#31-the-qc-default-used-log-system)
    - [3.1.1 For QNX, the default used log system is slog2.](#311-for-qnx-the-default-used-log-system-is-slog2)
    - [3.1.2 For Linux, the default used log system is syslog.](#312-for-linux-the-default-used-log-system-is-syslog)
  - [3.2 Direct the QC logger message to the standard output(stdout)](#32-direct-the-qc-logger-message-to-the-standard-outputstdout)
  - [3.3 Direct the QC logger message to a user-defined backend](#33-direct-the-qc-logger-message-to-a-user-defined-backend)
    - [3.3.1 Define a customized UserLoggerContext\_t for the Logger\_Handle\_t](#331-define-a-customized-userloggercontext_t-for-the-logger_handle_t)
    - [3.3.2 Implement a Logger\_Create\_t API that to create a logger handle](#332-implement-a-logger_create_t-api-that-to-create-a-logger-handle)
    - [3.3.3 Implement a Logger\_Log\_t API that to do message log](#333-implement-a-logger_log_t-api-that-to-do-message-log)
    - [3.3.4 Implement a Logger\_Destroy\_t API that to destroy the logger](#334-implement-a-logger_destroy_t-api-that-to-destroy-the-logger)
    - [3.3.5 Configure callbacks and initialize Logger with LOGGER\_BACKEND\_USER](#335-configure-callbacks-and-initialize-logger-with-logger_backend_user)
- [4. How to dynamic change the logger level through environment variable](#4-how-to-dynamic-change-the-logger-level-through-environment-variable)
  - [4.1 "QC\_LOG\_LEVEL" to control all the logger level.](#41-qc_log_level-to-control-all-the-logger-level)
  - [4.2 "${name}\_QC\_LOG\_LEVEL" to control the logger level of the logger with name.](#42-name_qc_log_level-to-control-the-logger-level-of-the-logger-with-name)

The QC logger is designed to be used by the QC components or utils only, and with customization, the log messages can be easily directed to the user application used log system.

# 1. QC Logger Types

- [Logger_Level_e](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)
- [LoggerBackend_e](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)
- [Logger_Handle_t](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)
- [Logger_Log_t](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)
- [Logger_Create_t](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)
- [Logger_Destroy_t](../include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp)


# 2. QC Logger Macros

Generally, the logger Macros are not suggested to be used by user application as it was design to be used by the QC components or utils only.

- [QC_DECLARE_LOGGER](../include/QC/Infras/Log/Logger.hpp#L34)
- [QC_LOGGER_INIT](../include/QC/Infras/Log/Logger.hpp#L46)
- [QC_LOGGER_DEINIT](../include/QC/Infras/Log/Logger.hpp#L49)
- [QC_LOGGER_LOG](../include/QC/Infras/Log/Logger.hpp#L62)
- [QC_VERBOSE](../include/QC/Infras/Log/Logger.hpp#L69)
- [QC_DEBUG](../include/QC/Infras/Log/Logger.hpp#L73)
- [QC_INFO](../include/QC/Infras/Log/Logger.hpp#L77)
- [QC_WARN](../include/QC/Infras/Log/Logger.hpp#L81)
- [QC_ERROR](../include/QC/Infras/Log/Logger.hpp#L85)
- [QC_LOG_VERBOSE](../include/QC/Infras/Log/Logger.hpp#L90)
- [QC_LOG_DEBUG](../include/QC/Infras/Log/Logger.hpp#L95)
- [QC_LOG_INFO](../include/QC/Infras/Log/Logger.hpp#L99)
- [QC_LOG_WARN](../include/QC/Infras/Log/Logger.hpp#L103)
- [QC_LOG_ERROR](../include/QC/Infras/Log/Logger.hpp#L107)

And please note that if the macro "DISABLE_QC_LOG" was defined, all the QC log will be disabled.

The Logger supports `LOGGER_BACKEND_SYSLOG`, `LOGGER_BACKEND_STDOUT`, and `LOGGER_BACKEND_USER`. `LOGGER_BACKEND_DEFAULT` resolves to the configured process default.
The runtime registry stores uniquely named Loggers without a fixed compile-time capacity. When a Logger is initialized with an already registered name, it is registered as `name_n`, where `n` is a per-name index that increases for the lifetime of the registry and is not reused after unregistration. A warning is printed to standard error. The final name is used by the backend and for the `<name>_QC_LOG_LEVEL` environment variable. Per-Logger runtime control takes a registered Logger name; unknown or empty names return `QC_STATUS_OUT_OF_BOUND`.
The default Logger is initialized on its first use. Concurrent calls to `Log()` are allowed; the thread-safety and message ordering of output are determined by the selected backend. Do not call `Deinit()` or `SwitchBackend()` while another thread is logging through the same Logger.

# 3. Typical Use Case

## 3.1 The QC default used log system

### 3.1.1 For QNX, the default used log system is slog2.

- [LoggerBackendSysLog](../source/Infras/Log/LoggerBackendSysLog_QNX.cpp)

Below is a demo command to check the QC logs:

```sh
slog2info | grep -i qcnode

# below command to clear logs
slog2info -c
```

### 3.1.2 For Linux, the default used log system is syslog.

- [LoggerBackendSysLog](../source/Infras/Log/LoggerBackendSysLog_Linux.cpp)

Below is a demo command to check the QC logs:

```sh
journalctl -o short-precise | grep -i qcnode

# below command to clear logs
journalctl --flush --rotate --vacuum-size=1
journalctl --flush --rotate --vacuum-time=1s
```

## 3.2 Direct the QC logger message to the standard output(stdout)

Use the built-in `LOGGER_BACKEND_STDOUT` backend when initializing an individual Logger. It writes each message to standard output with the Logger name as a prefix.

```c++
#include "QC/Infras/Log/Logger.hpp"

QC::Logger logger;
if ( QC::QC_STATUS_OK ==
     logger.Init( "APP", QC::LOGGER_LEVEL_INFO, QC::LOGGER_BACKEND_STDOUT ) )
{
    logger.Log( QC::LOGGER_LEVEL_INFO, "Application started" );
    (void) logger.Deinit();
}
```

To direct the default Logger used by `QC_LOG_*` macros to standard output, set its backend before the first call that uses the default Logger:

```c++
(void) QC::Logger::SetDefaultBackend( QC::LOGGER_BACKEND_STDOUT );
QC_LOG_INFO( "Application started" );
```

## 3.3 Direct the QC logger message to a user-defined backend

Use `LoggerUser` when messages must be sent to an application-specific logging system.

### 3.3.1 Define a customized UserLoggerContext_t for the Logger_Handle_t

```c++
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Log/LoggerUser.hpp"

#include <new>
#include <stdio.h>
#include <string>

using namespace QC;

struct UserLoggerContext_t
{
    std::string name;
};
```

### 3.3.2 Implement a Logger_Create_t API that to create a logger handle

The create callback allocates and returns one context for each Logger instance.

```c++
static QCStatus_e UserCreate( const char *pName, Logger_Level_e, Logger_Handle_t *pHandle )
{
    if ( ( nullptr == pName ) || ( nullptr == pHandle ) )
    {
        return QC_STATUS_BAD_ARGUMENTS;
    }

    UserLoggerContext_t *pContext = new ( std::nothrow ) UserLoggerContext_t{ pName };
    if ( nullptr == pContext )
    {
        return QC_STATUS_NOMEM;
    }
    *pHandle = static_cast<Logger_Handle_t>( pContext );
    return QC_STATUS_OK;
}
```

### 3.3.3 Implement a Logger_Log_t API that to do message log

The log callback receives the Logger context and forwards the formatted message to the user logging system. This example writes it to standard output.

```c++
static void UserLog( Logger_Handle_t hHandle, Logger_Level_e, const char *pFormat, va_list args )
{
    const auto *pContext = static_cast<const UserLoggerContext_t *>( hHandle );
    if ( ( nullptr != pContext ) && ( nullptr != pFormat ) )
    {
        (void) printf( "%s: ", pContext->name.c_str() );
        (void) vprintf( pFormat, args );
        (void) printf( "\n" );
    }
}
```

### 3.3.4 Implement a Logger_Destroy_t API that to destroy the logger

```c++
static void UserDestroy( Logger_Handle_t hHandle )
{
    delete static_cast<UserLoggerContext_t *>( hHandle );
}
```

### 3.3.5 Configure callbacks and initialize Logger with LOGGER_BACKEND_USER

Configure the process-wide callbacks before any Logger uses `LOGGER_BACKEND_USER`. Repeating setup with the same callbacks is allowed; different callbacks are rejected.

```c++
QC::Logger logger;
if ( QC_STATUS_OK == QC::LoggerUser::Setup( UserLog, UserCreate, UserDestroy ) )
{
    if ( QC_STATUS_OK == logger.Init( "APP", LOGGER_LEVEL_INFO, LOGGER_BACKEND_USER ) )
    {
        logger.Log( LOGGER_LEVEL_INFO, "Application started" );
        (void) logger.Deinit();
    }
}
```

# 4. How to dynamic change the logger level through environment variable

The dynamic change of the logger level through the environment variable must be done before starting the application that use the QC logger.

## 4.1 "QC_LOG_LEVEL" to control all the logger level.

```sh
# setting all the loggers' level to VERBOSE
export QC_LOG_LEVEL=VERBOSE

# setting all the loggers' level to DEBUG
export QC_LOG_LEVEL=DEBUG

# setting all the loggers' level to INFO
export QC_LOG_LEVEL=INFO

# setting all the loggers' level to WARN
export QC_LOG_LEVEL=WARN

# setting all the loggers' level to ERROR
export QC_LOG_LEVEL=ERROR
```

## 4.2 "${name}_QC_LOG_LEVEL" to control the logger level of the logger with name.

Generally, it’s recommended that the logger’s name is in upper case.

For example, for a camera component that using the QC logger with name "CAM0", the logger level can be changed through environment variable "CAM0_QC_LOG_LEVEL".

```sh
# setting the component CAM0 logger level to VERBOSE
export CAM0_QC_LOG_LEVEL=VERBOSE

# setting the component CAM0 logger level to DEBUG
export CAM0_QC_LOG_LEVEL=DEBUG

# setting the component CAM0 logger level to INFO
export CAM0_QC_LOG_LEVEL=INFO

# setting the component CAM0 logger level to WARN
export CAM0_QC_LOG_LEVEL=WARN

# setting the component CAM0 logger level to ERROR
export CAM0_QC_LOG_LEVEL=ERROR
``````
