set( HEADERS_DIR
    ${PROJECT_SOURCE_DIR}/source/Node/Voxelization/
    ${PROJECT_SOURCE_DIR}/source/Library/OpenclIface/
    ${PROJECT_SOURCE_DIR}/tests/unit_test/Library/OpenCL/
)

add_library( OpenCLMock SHARED
    OpenCLMock.cpp
)

set_property( TARGET OpenCLMock PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET OpenCLMock PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( OpenCLMock PUBLIC
    ${HEADERS_DIR}
)

install( TARGETS OpenCLMock DESTINATION lib )
