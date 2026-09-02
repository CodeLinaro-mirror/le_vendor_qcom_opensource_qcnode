set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( QC_NODE_CAMERA_HEADERS
    ${HEADERS_DIR}/QC/Node/Camera.hpp
)

set( QC_NODE_CAMERA_SOURCES
    Camera.cpp
    CameraImpl.cpp
    CameraConfig.cpp
    CameraMonitor.cpp
)

set( TARGET_LIBRARIES QCNodeBase qcxclient qcxosal camera_metadata )

if( DEFINED QC_CAMERA_EXTRA_LIBS )
    list( APPEND TARGET_LIBRARIES ${QC_CAMERA_EXTRA_LIBS} )
endif()

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/system )
    list( APPEND TARGET_LIBRARIES qcxplatform cutils )
else()
    list( APPEND TARGET_LIBRARIES xml2 )
endif()

add_library( QCNodeCamera OBJECT ${QC_NODE_CAMERA_SOURCES} )

set_property( TARGET QCNodeCamera PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeCamera PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNodeCamera PUBLIC ${HEADERS_DIR} )
target_link_libraries( QCNodeCamera PUBLIC ${TARGET_LIBRARIES} )

install( FILES ${QC_NODE_CAMERA_HEADERS} DESTINATION include/QC/Node )