set( HEADERS_DIR 
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_PREFIX_PATH}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include )

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/system ${CMAKE_SYSROOT}/usr/include/WF )
endif()

set ( CAMBUFFERMGR_SRC CameraBufferManager.cpp )

add_library( CameraBufferManager STATIC ${CAMBUFFERMGR_SRC} )
target_include_directories( CameraBufferManager PUBLIC ${HEADERS_DIR} )

set( TARGET_LIBRARIES QCNode )

set_property( TARGET CameraBufferManager PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET CameraBufferManager PROPERTY POSITION_INDEPENDENT_CODE ON )