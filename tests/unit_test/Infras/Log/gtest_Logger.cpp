// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "gtest/gtest.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#define LOGGER_UNIT_TEST
#include "MockCLib.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Log/LoggerUser.hpp"
#include "QC/Infras/Log/QCLoggerRegistry.hpp"

using namespace QC;

typedef struct
{
    std::string name;
} Logger_HandleContextUser_t;

static char s_LoggerMsg[1024] = { 0 };

#define MSG_PREFIX( name, b )                                                                      \
    std::string( #name ) + " " + std::string( __FILE__ ) + ":" + std::to_string( __LINE__ - b ) +  \
            " "

static void UserLog( Logger_Handle_t hHandle, Logger_Level_e level, const char *pFormat,
                     va_list args )
{
    int len = 0;
    Logger_HandleContextUser_t *pContext = static_cast<Logger_HandleContextUser_t *>( hHandle );
    len = snprintf( s_LoggerMsg, sizeof( s_LoggerMsg ), "%s ", pContext->name.c_str() );
    (void) vsnprintf( &s_LoggerMsg[len], sizeof( s_LoggerMsg ) - len, pFormat, args );
}

static void UserLogAlt( Logger_Handle_t hHandle, Logger_Level_e level, const char *pFormat,
                        va_list args )
{
    int len = 0;
    Logger_HandleContextUser_t *pContext = static_cast<Logger_HandleContextUser_t *>( hHandle );
    len = snprintf( s_LoggerMsg, sizeof( s_LoggerMsg ), "%s ALT ", pContext->name.c_str() );
    (void) level;
    (void) vsnprintf( &s_LoggerMsg[len], sizeof( s_LoggerMsg ) - len, pFormat, args );
}

static QCStatus_e UserLoggerHandleCreate( const char *pName, Logger_Level_e level,
                                          Logger_Handle_t *pHandle )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( ( nullptr == pName ) || ( nullptr == pHandle ) )
    {
        ret = QC_STATUS_BAD_ARGUMENTS;
    }
    else
    {
        Logger_HandleContextUser_t *pContext = new ( std::nothrow ) Logger_HandleContextUser_t;
        if ( nullptr != pContext )
        {
            pContext->name = pName;
            (void) level;
            *pHandle = static_cast<Logger_Handle_t>( pContext );
        }
        else
        {
            ret = QC_STATUS_NOMEM;
        }
    }

    return ret;
}

static void UserLoggerHandleDestroy( Logger_Handle_t hHandle )
{
    Logger_HandleContextUser_t *pContext = static_cast<Logger_HandleContextUser_t *>( hHandle );
    if ( nullptr != pContext )
    {
        delete pContext;
    }
}


static void TestDefaultLogger()
{
    std::string rst;
    /* default logger level is ERROR */
    QC_LOG_WARN( "A warn messgae is ignored", (uint32_t) 1234, 1.23431 );
    ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
    s_LoggerMsg[0] = '\0';

    QC_LOG_ERROR( "A Fatal error: %.2f", 1.2345 );
    rst = MSG_PREFIX( QCNODE, 1 ) + std::string( "ERROR: A Fatal error: 1.23" );
    ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
    s_LoggerMsg[0] = '\0';
}

class LoggerTestUser
{
public:
    LoggerTestUser() {}
    ~LoggerTestUser() {}

    QCStatus_e Init( const char *pName, Logger_Level_e level )
    {
        return QC_LOGGER_INIT( pName, level );
    }

    QCStatus_e Deinit() { return QC_LOGGER_DEINIT(); }

    void TestLoggerVerbose()
    {
        std::string rst;
        QC_VERBOSE( "a=%d", 1234 );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "VERBOSE: a=1234" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        QC_VERBOSE( "str=%s a=%d", "hello world", 1234 );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "VERBOSE: str=hello world a=1234" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        QC_INFO( "a=%u c=%.3f", (uint32_t) 1234, 1.23431 );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "INFO: a=1234 c=1.234" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        QC_ERROR( "A Fatal error: 0x%x", 0xdeadbeef );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "ERROR: A Fatal error: 0xdeadbeef" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';
    }

    void TestLoggerInfo()
    {
        std::string rst;
        QC_DEBUG( "a debug message is ignored" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        QC_INFO( "a=%u c=%.3f", (uint32_t) 1234, 1.23431 );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "INFO: a=1234 c=1.234" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        QC_ERROR( "A Fatal error: 0x%x", 0xdeadbeef );
        rst = MSG_PREFIX( TEST, 1 ) + std::string( "ERROR: A Fatal error: 0xdeadbeef" );
        ASSERT_EQ( rst, std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';
    }

private:
    QC_DECLARE_LOGGER();
};

static void TestBackendLog( const QCLoggerBackendIfs &backend, Logger_Level_e level,
                            const char *pFormat, ... )
{
    va_list args;
    va_start( args, pFormat );
    backend.Log( level, pFormat, args );
    va_end( args );
}

TEST( Logger, UserBackendBeforeSetup )
{
    LoggerUser user;

    ASSERT_EQ( QC_STATUS_OK, user.Init( "USER", LOGGER_LEVEL_INFO ) );
    TestBackendLog( user, LOGGER_LEVEL_INFO, "ignored" );
    ASSERT_EQ( QC_STATUS_OK, user.Deinit() );
}

TEST( Logger, SANITY_Logger )
{
    QCStatus_e ret;
    LoggerTestUser loggerUser;

    ret = LoggerUser::Setup( UserLog, UserLoggerHandleCreate, UserLoggerHandleDestroy );
    ASSERT_EQ( QC_STATUS_OK, ret );

    /* The user backend setup is process-wide and idempotent for the same callbacks. */
    ret = LoggerUser::Setup( UserLog, UserLoggerHandleCreate, UserLoggerHandleDestroy );
    ASSERT_EQ( QC_STATUS_OK, ret );

    unsetenv( "QC_LOG_LEVEL" );
    unsetenv( "TEST_QC_LOG_LEVEL" );

    ret = loggerUser.Init( "TEST", LOGGER_LEVEL_VERBOSE );
    ASSERT_EQ( QC_STATUS_OK, ret );
    loggerUser.TestLoggerVerbose();
    ret = loggerUser.Deinit();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = loggerUser.Init( "TEST", LOGGER_LEVEL_INFO );
    ASSERT_EQ( QC_STATUS_OK, ret );
    loggerUser.TestLoggerInfo();
    ret = loggerUser.Deinit();
    ASSERT_EQ( QC_STATUS_OK, ret );

    TestDefaultLogger();
}

static void TestLoggerLog2( Logger &logger, Logger_Level_e level, const char *pFormat, ... )
{
    va_list args;

    va_start( args, pFormat );
    logger.Log( level, pFormat, args );
    va_end( args );
}

TEST( Logger, L2_Logger )
{

    (void) LoggerUser::Setup( UserLog, UserLoggerHandleCreate, UserLoggerHandleDestroy );

    unsetenv( "QC_LOG_LEVEL" );
    unsetenv( "TEST_L2_QC_LOG_LEVEL" );

    {
        QCStatus_e ret;
        Logger logger;

        logger.Log( LOGGER_LEVEL_ERROR, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        TestLoggerLog2( logger, LOGGER_LEVEL_ERROR, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Init( nullptr, LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_MAX );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

        ret = logger.Init( "TEST_L2", (Logger_Level_e) -7 );
        ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_OK, ret );

        TestLoggerLog2( logger, LOGGER_LEVEL_VERBOSE, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        TestLoggerLog2( logger, LOGGER_LEVEL_ERROR, "Hello %d", 1234 );
        ASSERT_EQ( std::string( "TEST_L2 Hello 1234" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_BAD_STATE, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "VERBOSE", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_VERBOSE, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "DEBUG", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_VERBOSE, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        logger.Log( LOGGER_LEVEL_DEBUG, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "INFO", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_DEBUG );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_DEBUG, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        logger.Log( LOGGER_LEVEL_INFO, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "WARN", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_ERROR );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_INFO, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        logger.Log( LOGGER_LEVEL_WARN, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "ERROR", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_VERBOSE );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_WARN, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        logger.Log( LOGGER_LEVEL_ERROR, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    {
        QCStatus_e ret;
        Logger logger;
        setenv( "TEST_L2_QC_LOG_LEVEL", "invalid_level", 1 );
        ret = logger.Init( "TEST_L2", LOGGER_LEVEL_INFO );
        ASSERT_EQ( QC_STATUS_OK, ret );

        logger.Log( LOGGER_LEVEL_DEBUG, "Hello" );
        ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        logger.Log( LOGGER_LEVEL_INFO, "Hello" );
        ASSERT_EQ( std::string( "TEST_L2 Hello" ), std::string( s_LoggerMsg ) );
        s_LoggerMsg[0] = '\0';

        ret = logger.Deinit();
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    unsetenv( "TEST_L2_QC_LOG_LEVEL" );
}

TEST( Logger, RuntimeControlAndRegistry )
{
    Logger loggerA;
    Logger loggerB;
    QCLoggerRegistry &registry = QCLoggerRegistry::Instance();
    uint32_t count = 0U;
    ASSERT_EQ( QC_STATUS_OK, registry.GetNumber( count ) );
    ASSERT_EQ( QC_STATUS_OK,
               loggerA.Init( "LOGGER_RUNTIME_A", LOGGER_LEVEL_ERROR, LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_OK, loggerB.Init( "LOGGER_RUNTIME_B", LOGGER_LEVEL_INFO, LOGGER_BACKEND_STDOUT ) );

    ASSERT_EQ( QC_STATUS_OK, registry.GetNumber( count ) );
    ASSERT_GE( count, 2U );
    ASSERT_EQ( QC_STATUS_NO_RESOURCE, registry.GetLogger( nullptr, 0U ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, registry.GetLogger( nullptr, count ) );
    std::vector<QCLoggerRegistry::LoggerName_t> names( count );
    ASSERT_EQ( QC_STATUS_OK, registry.GetLogger( names.data(), count ) );
    ASSERT_NE( std::find_if( names.begin(), names.end(),
                             []( const auto &name ) {
                                 return 0 == std::strcmp( name.CStr(), "LOGGER_RUNTIME_A" );
                             } ),
               names.end() );
    ASSERT_NE( std::find_if( names.begin(), names.end(),
                             []( const auto &name ) {
                                 return 0 == std::strcmp( name.CStr(), "LOGGER_RUNTIME_B" );
                             } ),
               names.end() );

    ASSERT_EQ( QC_STATUS_OK, registry.SetLogLevel( "LOGGER_RUNTIME_A", LOGGER_LEVEL_DEBUG ) );
    ASSERT_EQ( LOGGER_LEVEL_DEBUG, loggerA.GetLevel() );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, loggerA.SetLevel( LOGGER_LEVEL_MAX ) );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND,
               registry.SetLogLevel( "LOGGER_RUNTIME_UNKNOWN", LOGGER_LEVEL_INFO ) );

    ASSERT_EQ( QC_STATUS_OK, registry.SetLogBackend( "LOGGER_RUNTIME_A", LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               loggerA.SwitchBackend( static_cast<LoggerBackend_e>( LOGGER_BACKEND_MAX ) ) );

    Logger duplicate;
    ASSERT_EQ( QC_STATUS_OK,
               duplicate.Init( "LOGGER_RUNTIME_A", LOGGER_LEVEL_INFO, LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( std::string( "LOGGER_RUNTIME_A_1" ), std::string( duplicate.GetName() ) );
    ASSERT_EQ( QC_STATUS_OK, registry.SetLogLevel( "LOGGER_RUNTIME_A_1", LOGGER_LEVEL_DEBUG ) );
    ASSERT_EQ( LOGGER_LEVEL_DEBUG, duplicate.GetLevel() );
    ASSERT_EQ( QC_STATUS_OK, duplicate.Deinit() );

    Logger duplicateAfterUnregister;
    ASSERT_EQ( QC_STATUS_OK, duplicateAfterUnregister.Init( "LOGGER_RUNTIME_A", LOGGER_LEVEL_INFO,
                                                            LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( std::string( "LOGGER_RUNTIME_A_2" ),
               std::string( duplicateAfterUnregister.GetName() ) );
    ASSERT_EQ( QC_STATUS_OK, duplicateAfterUnregister.Deinit() );
}

TEST( Logger, RegistrySupportsMoreThanLegacyLimit )
{
    constexpr uint32_t loggerCount = 129U;
    std::vector<std::unique_ptr<Logger>> loggers;
    loggers.reserve( loggerCount );

    for ( uint32_t index = 0U; index < loggerCount; ++index )
    {
        std::unique_ptr<Logger> pLogger = std::make_unique<Logger>();
        const std::string name = "LOGGER_REGISTRY_" + std::to_string( index );
        ASSERT_EQ( QC_STATUS_OK,
                   pLogger->Init( name.c_str(), LOGGER_LEVEL_ERROR, LOGGER_BACKEND_STDOUT ) );
        loggers.push_back( std::move( pLogger ) );
    }

    uint32_t count = 0U;
    ASSERT_EQ( QC_STATUS_OK, QCLoggerRegistry::Instance().GetNumber( count ) );
    ASSERT_GE( count, loggerCount );
}

TEST( Logger, InvalidStateAndGlobalControls )
{
    Logger logger;
    QCLoggerRegistry &registry = QCLoggerRegistry::Instance();
    std::string longName( 128U, 'L' );

    ASSERT_EQ( QC_STATUS_BAD_STATE, logger.Deinit() );
    ASSERT_EQ( QC_STATUS_BAD_STATE, logger.SwitchBackend( LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, logger.Init( "", LOGGER_LEVEL_INFO ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, logger.Init( longName.c_str(), LOGGER_LEVEL_INFO ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               logger.Init( "LOGGER_INVALID_BACKEND", LOGGER_LEVEL_INFO,
                            static_cast<LoggerBackend_e>( LOGGER_BACKEND_MAX ) ) );

    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Logger::SetDefaultLevel( LOGGER_LEVEL_MAX ) );
    ASSERT_EQ( QC_STATUS_OK, Logger::SetDefaultLevel( LOGGER_LEVEL_DEBUG ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, Logger::SetDefaultBackend( LOGGER_BACKEND_DEFAULT ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               Logger::SetDefaultBackend( static_cast<LoggerBackend_e>( LOGGER_BACKEND_MAX ) ) );
    ASSERT_EQ( QC_STATUS_OK, Logger::SetDefaultBackend( LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               LoggerUser::Setup( nullptr, UserLoggerHandleCreate, UserLoggerHandleDestroy ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               LoggerUser::Setup( UserLog, nullptr, UserLoggerHandleDestroy ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               LoggerUser::Setup( UserLog, UserLoggerHandleCreate, nullptr ) );
    ASSERT_EQ( QC_STATUS_OK,
               LoggerUser::Setup( UserLog, UserLoggerHandleCreate, UserLoggerHandleDestroy ) );
    ASSERT_EQ( QC_STATUS_BAD_STATE,
               LoggerUser::Setup( UserLogAlt, UserLoggerHandleCreate, UserLoggerHandleDestroy ) );

    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, registry.SetLogLevel( nullptr, LOGGER_LEVEL_INFO ) );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, registry.SetLogBackend( "", LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_OK,
               logger.Init( "LOGGER_GLOBAL_CONTROL", LOGGER_LEVEL_INFO, LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, registry.SetLogLevel( LOGGER_LEVEL_MAX ) );
    ASSERT_EQ( QC_STATUS_OK, registry.SetLogLevel( LOGGER_LEVEL_DEBUG ) );
    ASSERT_EQ( LOGGER_LEVEL_DEBUG, logger.GetLevel() );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS,
               registry.SetLogBackend( static_cast<LoggerBackend_e>( LOGGER_BACKEND_MAX ) ) );
    ASSERT_EQ( QC_STATUS_OK, registry.SetLogBackend( LOGGER_BACKEND_SYSLOG ) );
    ASSERT_EQ( QC_STATUS_OK, logger.SwitchBackend( LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_OK, logger.Deinit() );
    ASSERT_EQ( QC_STATUS_BAD_STATE, logger.Deinit() );
}

TEST( Logger, BackendSelectionAndLogGuards )
{
    Logger userLogger;
    Logger syslogLogger;

    unsetenv( "LOGGER_BACKEND_QC_LOG_LEVEL" );
    setenv( "QC_LOG_LEVEL", "DEBUG", 1 );
    ASSERT_EQ( QC_STATUS_OK, userLogger.Init( "LOGGER_BACKEND", LOGGER_LEVEL_ERROR, LOGGER_BACKEND_USER ) );
    ASSERT_EQ( LOGGER_LEVEL_DEBUG, userLogger.GetLevel() );

    userLogger.Log( LOGGER_LEVEL_DEBUG, "backend user message" );
    ASSERT_EQ( std::string( "LOGGER_BACKEND backend user message" ), std::string( s_LoggerMsg ) );
    s_LoggerMsg[0] = '\0';

    userLogger.Log( LOGGER_LEVEL_ERROR, nullptr );
    userLogger.Log( static_cast<Logger_Level_e>( LOGGER_LEVEL_MAX ), "invalid level" );
    ASSERT_EQ( std::string( "" ), std::string( s_LoggerMsg ) );

    ASSERT_EQ( QC_STATUS_OK, userLogger.SwitchBackend( LOGGER_BACKEND_STDOUT ) );
    testing::internal::CaptureStdout();
    userLogger.Log( LOGGER_LEVEL_ERROR, "stdout message" );
    ASSERT_EQ( std::string( "LOGGER_BACKEND: stdout message\n" ),
               testing::internal::GetCapturedStdout() );

    testing::internal::CaptureStdout();
    userLogger.Log( LOGGER_LEVEL_ERROR, "stdout message with newline\n" );
    ASSERT_EQ( std::string( "LOGGER_BACKEND: stdout message with newline\n" ),
               testing::internal::GetCapturedStdout() );
    ASSERT_EQ( QC_STATUS_OK, userLogger.Deinit() );

    ASSERT_EQ( QC_STATUS_OK,
               syslogLogger.Init( "LOGGER_SYSLOG", LOGGER_LEVEL_INFO, LOGGER_BACKEND_SYSLOG ) );
    syslogLogger.Log( LOGGER_LEVEL_INFO, "syslog message" );
    ASSERT_EQ( QC_STATUS_OK, syslogLogger.SwitchBackend( LOGGER_BACKEND_STDOUT ) );
    ASSERT_EQ( QC_STATUS_OK, syslogLogger.Deinit() );

    unsetenv( "QC_LOG_LEVEL" );
}

#ifndef GTEST_QCNODE
#if __CTC__
extern "C" void ctc_append_all( void );
#endif
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    int nVal = RUN_ALL_TESTS();
#if __CTC__
    ctc_append_all();
#endif
    return nVal;
}
#endif
