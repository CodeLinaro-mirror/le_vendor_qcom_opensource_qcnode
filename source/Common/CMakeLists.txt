set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( COMMON_HEADERS ${HEADERS_DIR}/QC/Common/Types.hpp )

set( COMMON_SOURCES
    Logger.cpp
)

set( TARGET_LIBRARIES )

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64" )
    list( APPEND COMMON_SOURCES Logger_Linux.cpp )
    list( APPEND TARGET_LIBRARIES pthread dl systemd )
else()
    list( APPEND COMMON_SOURCES Logger_QNX.cpp )
    if(ENABLE_SIMNODE)
        list( APPEND TARGET_LIBRARIES slog2 )
    else()
        list( APPEND TARGET_LIBRARIES libstd slog2 )
    endif()
endif()

add_library( QCNodeCommon OBJECT ${COMMON_SOURCES} )

set_property( TARGET QCNodeCommon PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeCommon PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNodeCommon PUBLIC ${HEADERS_DIR} )

target_link_libraries( QCNodeCommon PUBLIC ${TARGET_LIBRARIES} )

target_compile_options( QCNodeCommon PRIVATE -Wall )

install( FILES ${COMMON_HEADERS} DESTINATION include/QC/Common )
install( FILES ${PROJECT_SOURCE_DIR}/include/QC/Infras/Log/Logger.hpp DESTINATION include/QC/Infras/Log )