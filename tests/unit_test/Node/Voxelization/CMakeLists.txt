if( NOT TARGET QCNodeVoxelization )
    return()
endif()

set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include
                 ${PROJECT_SOURCE_DIR}/tests/utils 
                 ${PROJECT_SOURCE_DIR}/source/Node/Voxelization
                 ${PROJECT_SOURCE_DIR}/source/Library/OpenclIface/
                 ${PROJECT_SOURCE_DIR}/source/Library/FadasIface
                 ${PROJECT_SOURCE_DIR}/tests/unit_test/Library/OpenCL/
)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR  ${CMAKE_SYSROOT}/usr/include/amss/multimedia/fadas )
endif()

add_library( VoxelSdkForTest STATIC
    ${PROJECT_SOURCE_DIR}/source/Node/Voxelization/Voxelization.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Voxelization/VoxelizationImpl.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Voxelization/VoxelizationConfig.cpp
    ${PROJECT_SOURCE_DIR}/source/Node/Voxelization/VoxelizationMonitor.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/OpenclIface/OpenclIface.cpp
)

target_include_directories( VoxelSdkForTest PUBLIC
    ${HEADERS_DIR}
)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
target_compile_definitions(VoxelSdkForTest PUBLIC CL_TARGET_OPENCL_VERSION=200)
else()
target_compile_definitions(VoxelSdkForTest PUBLIC CL_TARGET_OPENCL_VERSION=300)
endif()

add_executable( gtest_NodeVoxelization 
    gtest_NodeVoxelization.cpp 
)

target_include_directories( gtest_NodeVoxelization PUBLIC ${HEADERS_DIR})
 
target_link_libraries( gtest_NodeVoxelization
    gtest
    VoxelSdkForTest
    FadasIface
    QCNodeBase
    QCNodeTestUtils
    BufferManager
    OpenCLMock
)

install(TARGETS gtest_NodeVoxelization DESTINATION bin)
