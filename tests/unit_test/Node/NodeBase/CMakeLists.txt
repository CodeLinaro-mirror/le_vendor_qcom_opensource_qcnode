
set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/utils)
add_executable( gtest_NodeBase
    gtest_NodeBase.cpp
    gtest_DataTree.cpp
    gtest_NodeTrace.cpp
    ${PROJECT_SOURCE_DIR}/source/Infras/NodeTrace/NodeTrace.cpp )
target_compile_definitions( gtest_NodeBase PUBLIC QC_ENABLE_NODETRACE )
target_include_directories( gtest_NodeBase PUBLIC ${HEADERS_DIR})
target_link_libraries( gtest_NodeBase gtest QCNode QCNodeTestUtils BufferManager )
install(TARGETS gtest_NodeBase DESTINATION bin)
