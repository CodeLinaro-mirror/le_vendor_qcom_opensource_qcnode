set( HEADERS_DIR
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include )

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64" )
    list( APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/WF )
endif()

set ( HETERO_SCHEDULER_WRAPPER_SRC HeteroSchedulerWrapper.cpp )

add_library( HeteroSchedulerWrapper STATIC ${HETERO_SCHEDULER_WRAPPER_SRC} )
target_link_libraries( HeteroSchedulerWrapper PUBLIC sched-client )

target_include_directories( HeteroSchedulerWrapper PUBLIC  ${HEADERS_DIR} ${CMAKE_CURRENT_SOURCE_DIR} )

set_property( TARGET HeteroSchedulerWrapper PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET HeteroSchedulerWrapper PROPERTY POSITION_INDEPENDENT_CODE ON )

target_compile_options( HeteroSchedulerWrapper PRIVATE -Wall -Wno-error=uninitialized )

