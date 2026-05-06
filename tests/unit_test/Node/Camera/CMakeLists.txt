if( NOT TARGET QCNodeCamera )
    return()
endif()

set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include
                 ${PROJECT_SOURCE_DIR}/tests/utils 
                 ${PROJECT_SOURCE_DIR}/source/Node/Camera
)

add_library( CameraMock SHARED CameraMock.cpp )
set_property( TARGET CameraMock PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET CameraMock PROPERTY POSITION_INDEPENDENT_CODE ON )

add_library( CameraSdkForTest STATIC
    ${PROJECT_SOURCE_DIR}/source/Node/Camera/Camera.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Camera/CameraConfig.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Camera/CameraMonitor.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Camera/CameraImpl.cpp
)

target_include_directories( CameraSdkForTest PUBLIC
    ${HEADERS_DIR}
)
target_link_libraries( CameraSdkForTest QCNodeBase CameraMock )

add_executable( gtest_NodeCamera gtest_NodeCamera.cpp )
target_include_directories( gtest_NodeCamera PUBLIC ${HEADERS_DIR} ${CMAKE_SYSROOT}/usr/include/system )
target_link_libraries( gtest_NodeCamera gtest CameraSdkForTest QCNodeTestUtils QCNodeSample camera_metadata )
install( TARGETS gtest_NodeCamera DESTINATION bin )
install( TARGETS CameraMock DESTINATION lib )