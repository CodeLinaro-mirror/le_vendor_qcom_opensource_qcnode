set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( LOGGER_SOURCES
    Logger.cpp
    QCLoggerRegistry.cpp
    LoggerBackendStdout.cpp
    LoggerUser.cpp
)

set( TARGET_LIBRARIES )

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND LOGGER_SOURCES LoggerBackendSysLog_Linux.cpp )
    list( APPEND TARGET_LIBRARIES pthread dl systemd )
else()
    list( APPEND LOGGER_SOURCES LoggerBackendSysLog_QNX.cpp )
    list( APPEND TARGET_LIBRARIES libstd slog2 )
endif()

add_library( QCNodeLogger OBJECT ${LOGGER_SOURCES} )

set_property( TARGET QCNodeLogger PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeLogger PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNodeLogger PUBLIC ${HEADERS_DIR} )
target_link_libraries( QCNodeLogger PUBLIC ${TARGET_LIBRARIES} )
target_compile_options( QCNodeLogger PRIVATE -Wall )

install( FILES
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/Logger.hpp
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/LoggerUser.hpp
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/QCLoggerRegistry.hpp
    DESTINATION include/QC/Infras/Log )
install( FILES
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/Ifs/QCLoggerDefs.hpp
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/Ifs/QCLoggerIfs.hpp
    ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/Ifs/QCLoggerBackendIfs.hpp
    DESTINATION include/QC/Infras/Log/Ifs )
